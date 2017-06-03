/* Copyright (c) 2012-2017, Bin Wei <bin@vip.qq.com>
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
#ifndef CCBASE_CONCURRENT_PTR_
#define CCBASE_CONCURRENT_PTR_

#include <memory>
#include "ccbase/common.h"
#include "ccbase/memory_reclamation.h"

namespace ccb {

template <class T>
struct ConcurrentPtrScope {};

template <class ConcurrentPtrType>
class ConcurrentPtrReader {
 public:
   ConcurrentPtrReader(ConcurrentPtrType* cp) {
     ptr = cp->ReadLock();
   }
   ~ConcurrentPtrReader() {
     cp->ReadUnlock();
   }
   ConcurrentPtrType get() const {
     return ptr_;
   }
   ConcurrentPtrType operator->() const {
     return get();
   }

 private:
   CCB_NOT_COPYABLE_AND_MOVABLE(ConcurrentPtrReader);

   ConcurrentPtrType::PtrType ptr_;
};

template <class T,
          class Deleter = std::default_delete<T>,
          class Reclamation = EpochBasedReclamation<T, ConcurrentPtrScope<T>>>
class ConcurrentPtr {
 public:
  using PtrType = T*;
  using Reader = ConcurrentPtrReader<ConcurrentPtr>;

  ConcurrentPtr(T* ptr)
      : ptr_(ptr) {}
  ~ConcurrentPtr() {}

  T* ReadLock() const {
    return recl_.ReadLock(&ptr_);
  }
  void ReadUnlock() const {
    recl_.ReadUnlock();
  }

  void Reset(T* ptr) {
    T* old_ptr = ptr_.exchange(ptr, std::memory_order_seq_cst);
    recl_.Retire(old_ptr, Deletor());
  }

 private:
  CCB_NOT_COPYABLE_AND_MOVABLE(ConcurrentPtr);

  std::atomic<T*> ptr_;
  mutable PtrReclamationAdapter<Reclamation> recl_;
};

template <class T,
          class Deleter = std::default_delete<T>,
          class Reclamation = EpochBasedReclamation<T, ConcurrentPtrScope<T>>>
class ConcurrentSharedPtr : private ConcurrentPtr<std::shared_ptr<T>*,
                                                  Deleter, Reclamation> {
 public:
  ConcurrentSharedPtr(T* ptr)
      : ConcurrentPtr(ptr) {}
  ~ConcurrentSharedPtr() {}

  std::shared_ptr<T> Get() const {
    ConcurrentPtr::Reader reader(this);
    return *reader.get();
  }
  std::shared_ptr<T> operator->() const {
    return Get();
  }

  void Reset(T* ptr) {
    Reset(std::make_shared<T>(ptr));
  }
  void Reset(std::shared_ptr<T> shptr) {
    ptr = new std::shared_ptr<T>(std::move(shptr));
    ConcurrentPtr::Reset(ptr);
  }

 private:
  CCB_NOT_COPYABLE_AND_MOVABLE(ConcurrentSharedPtr);
};

}  // namespace ccb

#endif  // CCBASE_THREAD_H_
