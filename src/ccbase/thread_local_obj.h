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
#ifndef CCBASE_THREAD_LOCAL_OBJ_H_
#define CCBASE_THREAD_LOCAL_OBJ_H_

#include <array>
#include <atomic>
#include <unordered_map>

namespace ccb {

template <class T>
class ThreadLocalObj {
 public:
  ThreadLocalObj() {
    instance_id_ = next_instance_id_.fetch_add(1);
  }
  size_t instance_id() const {
    return instance_id_;
  }
  T& get() const {
    return (instance_id_ < kInstanceCacheSize ? tls_obj_cache_[instance_id_]
                                              : tls_obj_map_[instance_id_]);
  }

 private:
  CCB_NOT_COPYABLE_AND_MOVABLE(ThreadLocalObj);

  size_t instance_id_;
  static constexpr size_t kInstanceCacheSize = 64;
  static thread_local std::unordered_map<size_t, T> tls_obj_map_;
  static thread_local std::array<T, kInstanceCacheSize> tls_obj_cache_;
  static std::atomic<size_t> next_instance_id_;
};

template <class T>
thread_local std::unordered_map<size_t, T>
    ThreadLocalObj<T>::tls_obj_map_{128};

template <class T>
thread_local std::array<T, ThreadLocalObj<T>::kInstanceCacheSize>
    ThreadLocalObj<T>::tls_obj_cache_;

template <class T>
std::atomic<size_t>
    ThreadLocalObj<T>::next_instance_id_{0};

}  // namespace ccb

#endif  // CCBASE_THREAD_LOCAL_OBJ_H_
