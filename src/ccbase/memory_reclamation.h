/* Copyright (c) 2016-2017, Bin Wei <bin@vip.qq.com>
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * 
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * The names of its contributors may not be used to endorse or 
 * promote products derived from this software without specific prior 
 * written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#ifndef CCBASE_MEMORY_RECLAMATION_H_
#define CCBASE_MEMORY_RECLAMATION_H_

#include <assert.h>
#include <atomic>
#include <memory>
#include <vector>
#include <utility>
#include <algorithm>
#include <type_traits>
#include "ccbase/common.h"
#include "ccbase/closure.h"
#include "ccbase/accumulated_list.h"

namespace ccb {

/* Ref-count based memory relcamation
 */
template <class T>
class RefCountReclamation {
 public:
  RefCountReclamation() : ref_count_(0) {}

  void ReadLock() {
    ref_count_.fetch_add(1, std::memory_order_seq_cst);
  }

  void ReadUnlock() {
    ref_count_.fetch_sub(1, std::memory_order_seq_cst);
  }

  void Retire(T* ptr) {
    Retire(ptr, std::default_delete<T>());
  }

  template <class F>
  void Retire(T* ptr, F&& del_func) {
    if (!ptr) return;
    while (ref_count_.load(std::memory_order_acquire)) {}
    del_func(ptr);
  }

  struct Trait {
    using ReadLockPointer = std::false_type;
    using HasRetireCleanup = std::false_type;
  };

 private:
  CCB_NOT_COPYABLE_AND_MOVABLE(RefCountReclamation);

  std::atomic<size_t> ref_count_;
};


/* Epoch based memory relcamation
 */
template <class T, class ScopeT = T>
class EpochBasedReclamation {
 public:
  EpochBasedReclamation() = default;

  static void ReadLock() {
    ReaderThreadState* state = &state_list_.LocalNode()->reader_state;
    state->is_active.store(true, std::memory_order_relaxed);
    state->local_epoch.store(global_epoch_.load(std::memory_order_relaxed),
                             std::memory_order_relaxed);
    // memory_order_seq_cst is required to garentee that updated is_active
    // and local_epoch are visable to all threads before critical-section
    atomic_thread_fence(std::memory_order_seq_cst);
  }

  static void ReadUnlock() {
    ReaderThreadState* state = &state_list_.LocalNode()->reader_state;
    // memory_order_release is required when leaving cirtical-section
    state->is_active.store(false, std::memory_order_release);
  }

  static void Retire(T* ptr) {
    Retire(ptr, nullptr);
  }

  static void Retire(T* ptr, std::default_delete<T>) {
    Retire(ptr, nullptr);
  }

  /* Retire a pointer with user defined deletor
   * @ptr       pointer to the object to be retired
   * @del_func  deletor called when the retired object is reclaimed
   *
   * The object will be put into to a retire-list waiting for final reclamation
   * which happens when any possible reference to the retired object has gone.
   * IMPORTANT: caller must garantee @ptr has been consistently unreachable to
   * all threads (e.g. reset with memory_order_seq_cst)
   */
  template <class F>
  static void Retire(T* ptr, F&& del_func) {
    // safety guideline: object retired in epoch N can only be referenced by
    // readers running in epoch N or N-1.
    // - reader-get-epoch < reader-get-ref < ref-unreachable < get-retire-epoch
    //   so always have reader-epoch <= N
    // - if global-epoch is at least N all current and future readers live in
    //   epoch >= N-1
    TryReclaim();
    WriterThreadState* writer_state = &state_list_.LocalNode()->writer_state;
    writer_state->retire_lists[writer_state->retire_epoch % kEpochSlots]
                 .emplace_back(ptr, std::forward<F>(del_func));
    TryUpdateEpoch();
  }

  static void RetireCleanup() {
    for (size_t count = 0; count < kEpochSlots; ) {
      count += TryReclaim();
      TryUpdateEpoch();
    }
  }

  struct Trait {
    using ReadLockPointer = std::false_type;
    using HasRetireCleanup = std::true_type;
  };

 private:
  CCB_NOT_COPYABLE_AND_MOVABLE(EpochBasedReclamation);

  static size_t TryReclaim() {
    WriterThreadState* writer_state = &state_list_.LocalNode()->writer_state;
    uint64_t epoch = global_epoch_.load(std::memory_order_seq_cst);
    if (writer_state->retire_epoch != epoch) {
      size_t reclaim_slots = epoch - writer_state->retire_epoch;
      if (reclaim_slots > kEpochSlots) {
        reclaim_slots = kEpochSlots;
      }
      for (size_t i = 1; i <= reclaim_slots; i++) {
        auto& rlist = writer_state->retire_lists[
                          (writer_state->retire_epoch + i) % kEpochSlots];
        if (!rlist.empty()) rlist.clear();
      }
      writer_state->retire_epoch = epoch;
      return reclaim_slots;
    }
    return 0;
  }

  static void TryUpdateEpoch() {
    // safety proof:
    // - if any reader's ReadLock-store-fence happens before the load below,
    //   we must see the reader thread is active, and its local_epoch may lag
    //   behind the epoch in which the critial-code is actually running
    //   - if we see the reader's local-epoch is update, it must be true as we
    //     assume the lag can't be as large as 2^64
    //   - it we see the reader's local-epoch is old, it may be false-negative
    //     but it is just safe as we will do nothing
    uint64_t epoch = global_epoch_.load(std::memory_order_seq_cst);
    // - if any reader's ReadLock-store-fence happens after the load above,
    //   the reader's cirtial-code will be running in current global-epoch at
    //   least. So no matter what we see and which decision we made it is
    //   always safe.
    bool all_sync = true;
    state_list_.Travel([epoch, &all_sync](ThreadState* state) {
      if (state->reader_state.is_active.load(std::memory_order_seq_cst)) {
        all_sync = all_sync &&
            (state->reader_state.local_epoch.load(std::memory_order_relaxed)
                == epoch);
      }
    });
    if (all_sync) {
      global_epoch_.compare_exchange_weak(epoch, epoch + 1,
                                          std::memory_order_seq_cst,
                                          std::memory_order_relaxed);
    }
  }

  // reader state
  struct ReaderThreadState {
    std::atomic<bool> is_active;
    std::atomic<uint64_t> local_epoch;

    ReaderThreadState() : is_active(false), local_epoch(0) {}
  };

  // writer state
  struct RetireEntry {
    T* ptr;
    ClosureFunc<void(T*)> del_func;

    RetireEntry(T* p, ClosureFunc<void(T*)> f)
        : ptr(p), del_func(std::move(f)) {}
    RetireEntry(const RetireEntry&) = delete;
    RetireEntry(RetireEntry&& e) {
      // move may happen when retire_list.emplace_back
      ptr = e.ptr;
      e.ptr = nullptr;
      del_func = std::move(e.del_func);
    }
    void operator=(const RetireEntry&) = delete;
    void operator=(RetireEntry&&) = delete;
    ~RetireEntry() {
      if (ptr) {
        if (del_func)
          del_func(ptr);
        else
          std::default_delete<T>()(ptr);
      }
    }
  };

  static constexpr size_t kEpochSlots = 2;  // only need 2 in this impl

  struct WriterThreadState {
    uint64_t retire_epoch{0};
    std::vector<RetireEntry> retire_lists[kEpochSlots];

    ~WriterThreadState() {
      // do cleanup if we have retired pointers not reclaimed
      if (std::any_of(retire_lists, retire_lists + kEpochSlots,
                      [](const std::vector<RetireEntry>& v) {
                        return !v.empty();
                      })) {
        RetireCleanup();
      }
    }
  };

  // thread local state
  struct ThreadState {
    ReaderThreadState reader_state;
    WriterThreadState writer_state;
  };

  // static member variables
  static std::atomic<uint64_t> global_epoch_;
  static ThreadLocalList<ThreadState> state_list_;
};

template <class T, class ScopeT>
std::atomic<uint64_t> EpochBasedReclamation<T, ScopeT>::global_epoch_{0};

template <class T, class ScopeT>
ThreadLocalList<typename EpochBasedReclamation<T, ScopeT>::ThreadState>
EpochBasedReclamation<T, ScopeT>::state_list_;


/* Hazard pointer based memory relcamation
 */
template <class T, class ScopeT = T,
          size_t kHazardPtrNum = 1,
          size_t kReclaimThreshold = 8>
class HazardPtrReclamation {
 public:
  HazardPtrReclamation() = default;

  static void ReadLock(T* ptr, size_t index = 0) {
    assert(index < kHazardPtrNum);
    ReaderThreadState* state = &state_list_.LocalNode()->reader_state;
    // memory_order_seq_cst is required to garentee that hazard pointer
    // is visable to all threads before critical-section
    state->hazard_ptrs[index].store(ptr, std::memory_order_seq_cst);
  }

  static void ReadUnlock() {
    ReaderThreadState* state = &state_list_.LocalNode()->reader_state;
    for (auto& hp : state->hazard_ptrs) {
      // memory_order_release is required when leaving cirtical-section
      hp.store(nullptr, std::memory_order_release);
    }
  }

  static void Retire(T* ptr) {
    Retire(ptr, nullptr);
  }

  static void Retire(T* ptr, std::default_delete<T>) {
    Retire(ptr, nullptr);
  }

  /* Retire a pointer with user defined deletor
   * @ptr       pointer to the object to be retired
   * @del_func  deletor called when the retired object is reclaimed
   *
   * The object will be put into to a retire-list waiting for final reclamation
   * which happens when retire-list is long enough and no other reference.
   * IMPORTANT: caller must garantee @ptr has been consistently unreachable to
   * all threads (e.g. reset with memory_order_seq_cst)
   */
  template <class F>
  static void Retire(T* ptr, F&& del_func) {
    WriterThreadState* writer_state = &state_list_.LocalNode()->writer_state;
    writer_state->retire_list.emplace_back(ptr, std::forward<F>(del_func));
    if (writer_state->retire_list.size() >= kReclaimThreshold) {
      TryReclaim();
    }
  }

  static void RetireCleanup() {
    WriterThreadState* writer_state = &state_list_.LocalNode()->writer_state;
    while (!writer_state->retire_list.empty()) {
      TryReclaim();
    }
  }

  struct Trait {
    using ReadLockPointer = std::true_type;
    using HasRetireCleanup = std::true_type;
  };

 private:
  CCB_NOT_COPYABLE_AND_MOVABLE(HazardPtrReclamation);

  static void TryReclaim() {
    // safety proof:
    // - if we decide one pointer can't be reclaimed it's always safe
    // - if we decide one pointer can be reclaimed let's consider one reader:
    //   - if the reader's ReadLock complete after begining of checks below,
    //     the double check following ReadLock will never see retired pointers
    //     therefore the reclamation safety always hold
    //   - if the reader's ReadLock complete before checks below, and
    //     if the reader's ReadUnlock begin after checks done, the reader has
    //     no reference definitely, else
    //   - if the reader's ReadUnlock begin before checks done, only 2 cases:
    //     - the reader has a ref and we acquired the release of ReadUnlock
    //     - the reader has no ref at all
    //     in either case we can safely reclaim the pointer.

    // use thread local allocation as cache
    static thread_local std::vector<T*> hazard_ptr_vec;
    hazard_ptr_vec.clear();
    // collect and sort hazard-pointers
    state_list_.Travel([](ThreadState* state) {
      for (auto& hp : state->reader_state.hazard_ptrs) {
        T* ptr = hp.load(std::memory_order_seq_cst);
        if (ptr) hazard_ptr_vec.push_back(ptr);
      }
    });
    std::sort(hazard_ptr_vec.begin(), hazard_ptr_vec.end());
    // filter out hazard pointers (survivors) from retire list
    WriterThreadState* writer_state = &state_list_.LocalNode()->writer_state;
    size_t survivors = 0;
    for (auto& entry : writer_state->retire_list) {
      if (std::binary_search(hazard_ptr_vec.begin(),
                             hazard_ptr_vec.end(), entry.ptr)) {
        writer_state->retire_list[survivors++].swap(entry);
      }
    }
    // reclaim all entries except survivors
    writer_state->retire_list.resize(survivors);
  }

  // reader state
  struct ReaderThreadState {
    std::atomic<T*> hazard_ptrs[kHazardPtrNum];
  };

  // writer state
  struct RetireEntry {
    T* ptr;
    ClosureFunc<void(T*)> del_func;

    RetireEntry()
        : ptr(nullptr), del_func(nullptr) {
      assert(false);  // never called
    }
    RetireEntry(T* p, ClosureFunc<void(T*)> f)
        : ptr(p), del_func(std::move(f)) {}
    RetireEntry(const RetireEntry&) = delete;
    RetireEntry(RetireEntry&& e) {
      // move may happen when retire_list.emplace_back
      ptr = e.ptr;
      e.ptr = nullptr;
      del_func = std::move(e.del_func);
    }
    void operator=(const RetireEntry&) = delete;
    void operator=(RetireEntry&& e) = delete;
    void swap(RetireEntry& e) {
      if (this != &e) {
        std::swap(this->ptr, e.ptr);
        this->del_func.swap(e.del_func);
      }
    }
    ~RetireEntry() {
      if (ptr) {
        if (del_func)
          del_func(ptr);
        else
          std::default_delete<T>()(ptr);
      }
    }
  };

  struct WriterThreadState {
    std::vector<RetireEntry> retire_list;

    ~WriterThreadState() {
      // do cleanup if we have retired pointers not reclaimed
      if (!retire_list.empty()) {
        RetireCleanup();
      }
    }
  };

  // thread local state
  struct ThreadState {
    ReaderThreadState reader_state;
    WriterThreadState writer_state;
  };

  // static member variables
  static ThreadLocalList<ThreadState> state_list_;
};

template <class T, class ScopeT,
          size_t kHazardPtrNum,
          size_t kReclaimThreshold>
ThreadLocalList<typename HazardPtrReclamation<T, ScopeT, kHazardPtrNum,
    kReclaimThreshold>::ThreadState>
HazardPtrReclamation<T, ScopeT, kHazardPtrNum,
    kReclaimThreshold>::state_list_;


/* An adapter class for single pointer reclamation
 */
template <class T, class Reclamation>
class PtrReclamationAdapter : private Reclamation {
 public:
  PtrReclamationAdapter() = default;

  template <class R = T*>
  typename std::enable_if<Reclamation::Trait::ReadLockPointer::value,
                          R>::type
  ReadLock(const std::atomic<T*>* atomic_ptr) {
    T* ptr;
    do {
      ptr = atomic_ptr->load(std::memory_order_seq_cst);
      Reclamation::ReadLock(ptr, 0);
    } while (ptr != atomic_ptr->load(std::memory_order_seq_cst));
    return ptr;
  }

  template <class R = T*>
  typename std::enable_if<!Reclamation::Trait::ReadLockPointer::value,
                          R>::type
  ReadLock(const std::atomic<T*>* atomic_ptr) {
    Reclamation::ReadLock();
    return atomic_ptr->load(std::memory_order_acquire);
  }

  void ReadUnlock() {
    Reclamation::ReadUnlock();
  }

  void Retire(T* ptr) {
    Reclamation::Retire(ptr);
  }

  template <class F>
  void Retire(T* ptr, F&& del_func) {
    Reclamation::Retire(ptr, std::forward<F>(del_func));
  }

  template <class R = void>
  typename std::enable_if<Reclamation::Trait::HasRetireCleanup::value,
                          R>::type
  RetireCleanup() {
    Reclamation::RetireCleanup();
  }

  template <class R = void>
  typename std::enable_if<!Reclamation::Trait::HasRetireCleanup::value,
                          R>::type
  RetireCleanup() {
  }

 private:
  CCB_NOT_COPYABLE_AND_MOVABLE(PtrReclamationAdapter);
};

}  // namespace ccb

#endif  // CCBASE_MEMORY_RECLAMATION_H_
