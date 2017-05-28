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

#include <atomic>
#include "ccbase/closure.h"
#include "ccbase/accumulated_list.h"

namespace ccb {

template <class T>
class RefCountReclamation {
 public:
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
  void RetireCleanup() { /* do nothing */ }

 private:
  std::atomic<size_t> ref_count_{0};
};

template <class T, class ScopeT = T>
class EpochBasedReclamation {
 public:
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

 private:
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
    RetireEntry(RetireEntry&& e) {
      // move may happen when retire_list.emplace_back
      ptr = e.ptr;
      e.ptr = nullptr;
      del_func = std::move(e.del_func);
    }
    ~RetireEntry() {
      if (ptr) {
        if (del_func) del_func(ptr);
        else delete ptr;
      }
    }
  };
  static constexpr size_t kEpochSlots = 4;  // 3 is enough 4 is optimization
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

#if 0
template <class ScopeT, size_t kHazardNum = 1, size_t kReclaimThreshold = 16>
class HazardPtrReclamation {
 public:
  void ReadLock(void*);
  void ReadUnlock();
  template <class F> void Retire(void* ptr, F&& del_func);

 private:
  struct ThreadState {
    std::atomic<void*> hazard_ptrs[kHazardNum];
  }
};
#endif

}  // namespace ccb

#endif  // MEMORY_RECLAMATION_H_
