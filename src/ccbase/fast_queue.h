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
#ifndef CCBASE_FAST_QUEUE_H_
#define CCBASE_FAST_QUEUE_H_

#include <atomic>
#include <memory>
#include <utility>
#include "ccbase/eventfd.h"
#include "ccbase/common.h"

namespace ccb {

template <typename T, bool kEnableNotify = true>
class FastQueue {
 public:
  explicit FastQueue(size_t qlen);
  ~FastQueue();
  bool Push(const T& val);
  bool Push(T&& val);
  bool Pop(T* ptr);
  bool PopWait(T* ptr, int timeout = -1);

  size_t used_size() {
    size_t head = head_.load(std::memory_order_acquire);
    size_t tail = tail_.load(std::memory_order_acquire);
    return (tail >= head) ?  (tail - head) : (tail + qlen_ - head);
  }

  size_t free_size() {
    size_t head = head_.load(std::memory_order_acquire);
    size_t tail = tail_.load(std::memory_order_acquire);
    return (head > tail) ?  (head - 1 - tail) : (head - 1 + qlen_ - tail);
  }

 private:
  CCB_NOT_COPYABLE_AND_MOVABLE(FastQueue);

  void move_head() {
    size_t head = head_.load(std::memory_order_relaxed) + 1;
    if (head >= qlen_)
      head -= qlen_;
    head_.store(head, std::memory_order_release);
  }
  void move_tail() {
    size_t tail = tail_.load(std::memory_order_relaxed) + 1;
    if (tail >= qlen_)
      tail -= qlen_;
    tail_.store(tail, std::memory_order_release);
  }

  size_t qlen_;
  std::atomic<size_t> head_;
  std::atomic<size_t> tail_;
  std::unique_ptr<T[]> array_;
  std::unique_ptr<EventFd> event_;
};

template <typename T, bool kEnableNotify>
FastQueue<T, kEnableNotify>::FastQueue(size_t qlen)
    : qlen_(qlen), head_(0), tail_(0),
      array_(new T[qlen]), event_(new EventFd()) {
}

template <typename T, bool kEnableNotify>
FastQueue<T, kEnableNotify>::~FastQueue() {
}

template <typename T, bool kEnableNotify>
bool FastQueue<T, kEnableNotify>::Push(const T& val) {
  if (free_size() <= 0) {
    return false;
  }
  array_[tail_.load(std::memory_order_relaxed)] = val;
  move_tail();
  if (kEnableNotify) {
    // StoreLoad order require barrier
    std::atomic_thread_fence(std::memory_order_seq_cst);
    if (used_size() == 1) {
      event_->Notify();
    }
  }
  return true;
}

template <typename T, bool kEnableNotify>
bool FastQueue<T, kEnableNotify>::Push(T&& val) {
  if (free_size() <= 0) {
    return false;
  }
  array_[tail_.load(std::memory_order_relaxed)] = std::move(val);
  move_tail();
  if (kEnableNotify) {
    // the memory fence garentees that the pushed node is visiable to all
    // threads before checking condition of notification
    std::atomic_thread_fence(std::memory_order_seq_cst);
    if (used_size() == 1) {
      event_->Notify();
    }
  }
  return true;
}

template <typename T, bool kEnableNotify>
bool FastQueue<T, kEnableNotify>::Pop(T* ptr) {
  if (kEnableNotify) {
    // the memory fence garentees that any previous pop is visible to all
    // threads before checking new node, therefore if used_size > 1 is found
    // when new node is pushed PopWait() will never miss it before blocking
    std::atomic_thread_fence(std::memory_order_seq_cst);
  }
  if (used_size() <= 0) {
    return false;
  }
  *ptr = std::move(array_[head_.load(std::memory_order_relaxed)]);
  move_head();
  return true;
}

template <typename T, bool kEnableNotify>
bool FastQueue<T, kEnableNotify>::PopWait(T* ptr, int timeout) {
  if (kEnableNotify) {
    while (!Pop(ptr)) {
      if (!event_->GetWait(timeout)) {
        return false;
      }
    }
  } else {
    int sleep_ms = 0;
    while (!Pop(ptr)) {
      if (timeout >= 0 && sleep_ms >= timeout)
        return false;
      usleep(1000);
      sleep_ms++;
    }
  }
  return true;
}

}  // namespace ccb

#endif  // CCBASE_FAST_QUEUE_H_
