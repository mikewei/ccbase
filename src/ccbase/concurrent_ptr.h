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
#ifndef CCBASE_CONCURRENT_PTR_H_
#define CCBASE_CONCURRENT_PTR_H_

#include <memory>
#include <utility>
#include "ccbase/common.h"
#include "ccbase/memory_reclamation.h"

namespace ccb {

template <class T>
struct ConcurrentPtrScope {};

template <class ConcurrentPtrType>
class ConcurrentPtrReader {
 public:
  explicit ConcurrentPtrReader(const ConcurrentPtrType* cp)
      : cp_(cp) {
    ptr_ = cp->ReadLock();
  }

  ~ConcurrentPtrReader() {
    cp_->ReadUnlock();
  }

  typename ConcurrentPtrType::PtrType get() const {
    return ptr_;
  }

  typename ConcurrentPtrType::PtrType operator->() const {
    return get();
  }

 private:
  CCB_NOT_COPYABLE_AND_MOVABLE(ConcurrentPtrReader);

  const ConcurrentPtrType* cp_;
  typename ConcurrentPtrType::PtrType ptr_;
};


template <class T,
          class Deleter = std::default_delete<T>,
          class Reclamation = EpochBasedReclamation<T, ConcurrentPtrScope<T>>>
class ConcurrentPtr {
 public:
  using PtrType = T*;
  using Reader = ConcurrentPtrReader<ConcurrentPtr>;

  ConcurrentPtr()
      : ptr_(nullptr) {}

  explicit ConcurrentPtr(std::nullptr_t)
      : ptr_(nullptr) {}

  explicit ConcurrentPtr(T* ptr)
      : ptr_(ptr) {}

  /* Destructor
   *
   * Caller should garentee that no one could race with the destruction
   */
  ~ConcurrentPtr() {
    if (ptr_) Deleter()(ptr_);
  }

  T* ReadLock() const {
    return recl_.ReadLock(&ptr_);
  }

  void ReadUnlock() const {
    recl_.ReadUnlock();
  }

  void Reset(bool sync_cleanup = false) {
    Reset(nullptr, sync_cleanup);
  }

  void Reset(T* ptr, bool sync_cleanup = false) {
    T* old_ptr = ptr_.exchange(ptr, std::memory_order_seq_cst);
    if (old_ptr)
      recl_.Retire(old_ptr, Deleter());
    if (sync_cleanup)
      recl_.RetireCleanup();
  }

 private:
  CCB_NOT_COPYABLE_AND_MOVABLE(ConcurrentPtr);

  std::atomic<T*> ptr_;
  mutable PtrReclamationAdapter<T, Reclamation> recl_;
};


template <class T,
          class Deleter = std::default_delete<T>,
          class Reclamation = EpochBasedReclamation<std::shared_ptr<T>,
                                                    ConcurrentPtrScope<T>>>
class ConcurrentSharedPtr
    : private ConcurrentPtr<std::shared_ptr<T>,
                            std::default_delete<std::shared_ptr<T>>,
                            Reclamation> {
 public:
  using Base = ConcurrentPtr<std::shared_ptr<T>,
                             std::default_delete<std::shared_ptr<T>>,
                             Reclamation>;

  ConcurrentSharedPtr()
      : Base(new std::shared_ptr<T>(nullptr, Deleter())) {}

  explicit ConcurrentSharedPtr(std::nullptr_t)
      : Base(new std::shared_ptr<T>(nullptr, Deleter())) {}

  explicit ConcurrentSharedPtr(T* ptr)
      : Base(new std::shared_ptr<T>(ptr, Deleter())) {}

  explicit ConcurrentSharedPtr(std::shared_ptr<T> shptr)
      : Base(new std::shared_ptr<T>(std::move(shptr))) {}

  ~ConcurrentSharedPtr() {}

  std::shared_ptr<T> Get() const {
    typename Base::Reader reader(this);
    return *reader.get();
  }

  std::shared_ptr<T> operator->() const {
    return Get();
  }

  void Reset(bool sync_cleanup = false) {
    Base::Reset(new std::shared_ptr<T>(nullptr, Deleter()), sync_cleanup);
  }

  void Reset(T* ptr, bool sync_cleanup = false) {
    Base::Reset(new std::shared_ptr<T>(ptr, Deleter()), sync_cleanup);
  }

  void Reset(std::shared_ptr<T> shptr, bool sync_cleanup = false) {
    Base::Reset(new std::shared_ptr<T>(std::move(shptr)), sync_cleanup);
  }

 private:
  CCB_NOT_COPYABLE_AND_MOVABLE(ConcurrentSharedPtr);
};

}  // namespace ccb

#endif  // CCBASE_CONCURRENT_PTR_H_
