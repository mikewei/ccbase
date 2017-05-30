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
#ifndef MEMORY_RECLAMATION_H_
#define MEMORY_RECLAMATION_H_

#include <assert.h>
#include <atomic>
#include <vector>
#include <chrono>
#include <thread>
#include <algorithm>
#include <stdexcept>
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
  RefCountReclamation() = default;

  void ReadLock() {
    ref_count_.fetch_add(1, std::memory_order_seq_cst);
  }
  void ReadUnlock() {
    ref_count_.fetch_sub(1, std::memory_order_seq_cst);
  }
  void Retire(T* ptr) {
    Retire(ptr, [](T* p) {
      delete p;
    });
  }
  template <class F>
  void Retire(T* ptr, F&& del_func) {
    if (!ptr) return;
    while (ref_count_.load(std::memory_order_acquire)) {}
    del_func(ptr);
  }
  void RetireCleanup() {}

  struct Trait {
    using ReadLockPointer = std::false_type;
    using HasRetireCleanup = std::false_type;
  };

 private:
  CCB_NOT_COPYABLE_AND_MOVABLE(RefCountReclamation);

  std::atomic<size_t> ref_count_{0};
};


/* Epoch based memory relcamation
 */
template <class T, class ScopeT = T>
class EpochBasedReclamation {
 public:
  EpochBasedReclamation() = default;

  void ReadLock() {
    ReaderThreadState* state = reader_state_list_.LocalNode();
    state->is_active.store(true, std::memory_order_relaxed);
    state->local_epoch.store(global_epoch_.load(std::memory_order_relaxed),
                             std::memory_order_relaxed); 
    // memory_order_seq_cst is required to garentee that updated is_active
    // and local_epoch are visable to all threads before critical-section
    atomic_thread_fence(std::memory_order_seq_cst);
  }
  void ReadUnlock() {
    ReaderThreadState* state = reader_state_list_.LocalNode();
    // memory_order_release is required when leaving cirtical-section
    state->is_active.store(false, std::memory_order_release);
  }
  void Retire(T* ptr) {
    Retire(ptr, nullptr);
  }
  template <class F>
  void Retire(T* ptr, F&& del_func) {
    TryReclaim();
    writer_state_.retire_lists[writer_state_.retire_epoch % kEpochSlots]
                 .emplace_back(ptr, std::forward<F>(del_func));
    TryUpdateEpoch();
  }
  void RetireCleanup() {
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

  size_t TryReclaim() {
    uint64_t epoch = global_epoch_.load(std::memory_order_seq_cst);
    if (writer_state_.retire_epoch != epoch) {
      size_t reclaim_slots = epoch - writer_state_.retire_epoch;
      if (reclaim_slots > kEpochSlots) {
        reclaim_slots = kEpochSlots;
      }
      for (size_t i = 1; i <= reclaim_slots; i++) {
        auto& rlist = writer_state_.retire_lists[
                          (writer_state_.retire_epoch + i) % kEpochSlots];
        if (!rlist.empty()) rlist.clear();
      }
      writer_state_.retire_epoch = epoch;
      return reclaim_slots;
    }
    return 0;
  }
  void TryUpdateEpoch() {
    uint64_t epoch = global_epoch_.load(std::memory_order_seq_cst);
    bool all_sync = true;
    reader_state_list_.Travel([epoch, &all_sync](ReaderThreadState* state) {
      if (state->is_active.load(std::memory_order_seq_cst)) {
        all_sync = all_sync &&
            (state->local_epoch.load(std::memory_order_relaxed) == epoch);
      }
    });
    if (all_sync) {
      global_epoch_.compare_exchange_weak(epoch, epoch + 1,
                                          std::memory_order_seq_cst,
                                          std::memory_order_relaxed);
    }
  }
  // global epoch
  static std::atomic<uint64_t> global_epoch_;
  // reader state
  struct ReaderThreadState {
    std::atomic<bool> is_active{false};
    std::atomic<uint64_t> local_epoch{0};
  };
  using ThisType = EpochBasedReclamation<T, ScopeT>;
  ThreadLocalList<ReaderThreadState, ThisType> reader_state_list_;
  // writer state
  struct RetireEntry {
    T* ptr;
    ClosureFunc<void(T*)> del_func;

    RetireEntry(T* p, ClosureFunc<void(T*)> f = nullptr)
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
        if (del_func) del_func(ptr);
        else delete ptr;
      }
    }
  };
  static constexpr size_t kEpochSlots = 2;  // only need 2 in this impl
  struct WriterThreadState {
    size_t retire_epoch{0};
    std::vector<RetireEntry> retire_lists[kEpochSlots];
  };
  static thread_local WriterThreadState writer_state_;
};

template <class T, class ScopeT>
std::atomic<uint64_t> EpochBasedReclamation<T, ScopeT>::global_epoch_{0};

template <class T, class ScopeT> thread_local
typename EpochBasedReclamation<T, ScopeT>::WriterThreadState
EpochBasedReclamation<T, ScopeT>::writer_state_;


/* Hazard pointer based memory relcamation
 */
template <class T, class ScopeT = T,
          size_t kHazardPtrNum = 1,
          size_t kReclaimThreshold = 8>
class HazardPtrReclamation {
 public:
  HazardPtrReclamation() = default;

  void ReadLock(T* ptr, size_t index = 0) {
    assert(index < kHazardPtrNum);
    ReaderThreadState* state = reader_state_list_.LocalNode();
    // memory_order_seq_cst is required to garentee that hazard pointer
    // is visable to all threads before critical-section
    state->hazard_ptrs[index].store(ptr, std::memory_order_seq_cst);
  }
  void ReadUnlock() {
    ReaderThreadState* state = reader_state_list_.LocalNode();
    for (auto& hp : state->hazard_ptrs) {
      // memory_order_release is required when leaving cirtical-section
      hp.store(nullptr, std::memory_order_release);
    }
  }
  void Retire(T* ptr) {
    Retire(ptr, nullptr);
  }
  template <class F>
  void Retire(T* ptr, F&& del_func) {
    writer_state_.retire_list.emplace_back(ptr, std::forward<F>(del_func));
    if (writer_state_.retire_list.size() >= kReclaimThreshold) {
      TryReclaim();
    }
  }
  void RetireCleanup() {
    for (size_t i = 0; !writer_state_.retire_list.empty(); i++) {
      if (i > 1000UL) {
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
      }
      TryReclaim();
    }
  }

  struct Trait {
    using ReadLockPointer = std::true_type;
    using HasRetireCleanup = std::true_type;
  };

 private:
  CCB_NOT_COPYABLE_AND_MOVABLE(HazardPtrReclamation);

  void TryReclaim() {
    // use thread local allocation as cache
    static thread_local std::vector<T*> hazard_ptr_vec;
    hazard_ptr_vec.clear();
    // collect and sort hazard-pointers
    reader_state_list_.Travel([&hazard_ptr_vec](ReaderThreadState* state) {
      for (auto& hp : state->hazard_ptrs) {
        T* ptr = hp.load(std::memory_order_acquire);
        if (ptr) hazard_ptr_vec.push_back(ptr);
      }
    });
    std::sort(hazard_ptr_vec.begin(), hazard_ptr_vec.end());
    // filter out hazard pointers (survivors) from retire list
    size_t survivors = 0;
    for (auto& entry : writer_state_.retire_list) {
      if (std::binary_search(hazard_ptr_vec.begin(),
                             hazard_ptr_vec.end(), entry.ptr)) {
        writer_state_.retire_list[survivors++].swap(entry);
      }
    }
    // reclaim all entries except survivors
    writer_state_.retire_list.resize(survivors);
  }

  // reader state
  struct ReaderThreadState {
    std::atomic<T*> hazard_ptrs[kHazardPtrNum];
  };
  using ThisType = HazardPtrReclamation<T, ScopeT>;
  ThreadLocalList<ReaderThreadState, ThisType> reader_state_list_;
  // writer state
  struct RetireEntry {
    T* ptr;
    ClosureFunc<void(T*)> del_func;

    RetireEntry()
        : ptr(nullptr), del_func(nullptr) {
      assert(false);  // never called
    }
    RetireEntry(T* p, ClosureFunc<void(T*)> f = nullptr)
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
        if (del_func) del_func(ptr);
        else delete ptr;
      }
    }
  };
  struct WriterThreadState {
    std::vector<RetireEntry> retire_list;
  };
  static thread_local WriterThreadState writer_state_;
};

template <class T, class ScopeT,
          size_t kHazardPtrNum,
          size_t kReclaimThreshold> thread_local
typename HazardPtrReclamation<T, ScopeT,
                              kHazardPtrNum,
                              kReclaimThreshold>::WriterThreadState
HazardPtrReclamation<T, ScopeT,
                     kHazardPtrNum,
                     kReclaimThreshold>::writer_state_;


/* An adapter class for single pointer reclamation
 */
template <class T, class Reclamation>
class PtrReclamationAdapter : private Reclamation {
 public:
  PtrReclamationAdapter() = default;

  template <class R = T*>
  typename std::enable_if<Reclamation::Trait::ReadLockPointer::value,
                          R>::type
  ReadLock(std::atomic<T*>* atomic_ptr) {
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
  ReadLock(std::atomic<T*>* atomic_ptr) {
    Reclamation::ReadLock();
    return atomic_ptr->load(std::memory_order_acquire);
  }

  template <class R = T*>
  typename std::enable_if<Reclamation::Trait::ReadLockPointer::value,
                          R>::type
  ReadLock(T** raw_ptr) {
    T* ptr;
    do {
      ptr = *raw_ptr;
      Reclamation::ReadLock(ptr, 0);
    } while (ptr != *raw_ptr);
    return ptr;
  }

  template <class R = T*>
  typename std::enable_if<!Reclamation::Trait::ReadLockPointer::value,
                          R>::type
  ReadLock(T** raw_ptr) {
    Reclamation::ReadLock();
    return *raw_ptr;
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

#endif  // MEMORY_RECLAMATION_H_
