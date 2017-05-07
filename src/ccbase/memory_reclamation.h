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

namespace ccb {

template <class ScopeT>
class RefCountReclamation {
 public:
  void ReadLock(void*) {
    ref_count_.fetch_add(1, std::memory_order_seq_cst);
  }
  void ReadUnlock() {
    ref_count_.fetch_sub(1, std::memory_order_seq_cst);
  }
  template <class F>
  void Retire(void* ptr, F&& del_func) {
    if (!ptr) return;
    while (ref_count_.load(std::memory_order_relaxed)) {}
    del_func(ptr);
  }

 private:
  std::atomic<size_t> ref_count_{0};
};

template <class ScopeT>
class EpochBasedReclamation {
 public:
  void ReadLock(void*);
  void ReadUnlock();
  template <class F> void Retire(void* ptr, F&& del_func);

 private:
  struct ThreadState {
    std::atomic<bool>   is_active;
    std::atomic<size_t> local_epoch;
  }
  using StateList = TlsAccumulatedList<ThreadState>;
  struct RetireEntry {
    void* ptr;
    ClosureFunc<void(void*)> del_func;
  }
  static thread_local std::vector<RetireEntry> retire_list[3];
};


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

}  // namespace ccb

#endif  // MEMORY_RECLAMATION_H_
