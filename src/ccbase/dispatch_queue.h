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
#ifndef CCBASE_DISPATCH_QUEUE_H_
#define CCBASE_DISPATCH_QUEUE_H_

#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <mutex>
#include <vector>
#include <utility>
#include "ccbase/fast_queue.h"

namespace ccb {

template <typename T>
class DispatchQueue {
 public:
  class OutQueue {
   public:
    virtual ~OutQueue() {}
    virtual bool Push(const T& val) = 0;
    virtual bool Push(T&& val) = 0;
    virtual bool Push(size_t idx, const T& val) = 0;
    virtual bool Push(size_t idx, T&& val) = 0;
  };
  class InQueue {
   public:
    virtual ~InQueue() {}
    virtual bool Pop(T* ptr) = 0;
    virtual bool PopWait(T* ptr, int timeout) = 0;
  };

  explicit DispatchQueue(size_t qlen);
  virtual ~DispatchQueue();
  OutQueue* RegisterProducer();
  InQueue* RegisterConsumer();

 private:
  CCB_NOT_COPYABLE_AND_MOVABLE(DispatchQueue);

  size_t qlen_;
  typedef FastQueue<T, true> _Queue;
  std::mutex mutex_;

  class _Producer;
  class _Consumer;
  static const size_t kMaxProducers = 1024;
  static const size_t kMaxConsumers = 1024;
  // use plain array for lock-free appending
  _Producer* producers_[kMaxProducers];
  _Consumer* consumers_[kMaxConsumers];
  size_t producer_count_;
  size_t consumer_count_;
};

///////////////////////////////////////////////////////////////////

template <typename T>
class DispatchQueue<T>::_Producer : public DispatchQueue<T>::OutQueue {
 public:
  _Producer(DispatchQueue<T>* dq)
      : dispatch_queue_(dq), cur_index_(-1U) {
    memset(queue_vec, 0, sizeof(queue_vec));
  }
  bool Push(const T& val) override;
  bool Push(T&& val) override;
  bool Push(size_t idx, const T& val) override;
  bool Push(size_t idx, T&& val) override;
  _Queue* queue_vec[kMaxConsumers];
 private:
  DispatchQueue<T>* dispatch_queue_;
  size_t cur_index_;
};

template <typename T>
bool DispatchQueue<T>::_Producer::Push(const T& val) {
  size_t last_index = cur_index_;
  for (cur_index_++; cur_index_ < kMaxConsumers && queue_vec[cur_index_]; cur_index_++) {
    if (queue_vec[cur_index_]->Push(val))
      return true;
  }
  for (cur_index_=0; cur_index_ <= last_index && queue_vec[cur_index_]; cur_index_++) {
    if (queue_vec[cur_index_]->Push(val))
      return true;
  }
  return false;
}

template <typename T>
bool DispatchQueue<T>::_Producer::Push(T&& val) {
  size_t last_index = cur_index_;
  for (cur_index_++; cur_index_ < kMaxConsumers && queue_vec[cur_index_]; cur_index_++) {
    if (queue_vec[cur_index_]->Push(std::move(val)))
      return true;
  }
  for (cur_index_=0; cur_index_ <= last_index && queue_vec[cur_index_]; cur_index_++) {
    if (queue_vec[cur_index_]->Push(std::move(val)))
      return true;
  }
  return false;
}

template <typename T>
bool DispatchQueue<T>::_Producer::Push(size_t idx, const T& val) {
  if (queue_vec[idx] && queue_vec[idx]->Push(val))
      return true;
  return false;
}

template <typename T>
bool DispatchQueue<T>::_Producer::Push(size_t idx, T&& val) {
  if (queue_vec[idx] && queue_vec[idx]->Push(std::move(val)))
      return true;
  return false;
}

///////////////////////////////////////////////////////////////////

template <typename T>
class DispatchQueue<T>::_Consumer : public DispatchQueue<T>::InQueue {
 public:
  _Consumer(DispatchQueue<T>* dq)
      : dispatch_queue_(dq), cur_index_(-1U)
      , cur_index_read_cnt_(0) {
    memset(queue_vec, 0, sizeof(queue_vec));
  }
  bool Pop(T* ptr) override;
  bool PopWait(T* ptr, int timeout) override;
  _Queue* queue_vec[kMaxProducers];
 private:
  static const size_t kMaxStickyReadCnt = 32;
  DispatchQueue<T>* dispatch_queue_;
  size_t cur_index_;
  size_t cur_index_read_cnt_;
  int epfd;
};

template <typename T>
bool DispatchQueue<T>::_Consumer::Pop(T* ptr) {
  // sticky read for performance
  if (cur_index_read_cnt_ && cur_index_read_cnt_ < kMaxStickyReadCnt) {
    if (queue_vec[cur_index_]->Pop(ptr)) {
      cur_index_read_cnt_++;
      return true;
    }
  }
  cur_index_read_cnt_ = 0;

  size_t last_index = cur_index_;
  for (cur_index_++; cur_index_ < kMaxProducers && queue_vec[cur_index_]; cur_index_++) {
    if (queue_vec[cur_index_]->Pop(ptr)) {
      cur_index_read_cnt_ = 1;
      return true;
    }
  }
  for (cur_index_=0; cur_index_ <= last_index && queue_vec[cur_index_]; cur_index_++) {
    if (queue_vec[cur_index_]->Pop(ptr)) {
      cur_index_read_cnt_ = 1;
      return true;
    }
  }
  return false;
}

template <typename T>
bool DispatchQueue<T>::_Consumer::PopWait(T* ptr, int timeout) {
  // naive impl now
  int sleep_ms = 0;
  while (!Pop(ptr)) {
    if (timeout >= 0 && sleep_ms >= timeout)
      return false;
    usleep(1000);
    sleep_ms++;
  }
  return true;
}

///////////////////////////////////////////////////////////////////

template <typename T>
DispatchQueue<T>::DispatchQueue(size_t qlen)
  : qlen_(qlen), producer_count_(0), consumer_count_(0) {
  memset(producers_, 0, sizeof(producers_));
  memset(consumers_, 0, sizeof(consumers_));
}

template <typename T>
DispatchQueue<T>::~DispatchQueue() {
  for (size_t i = 0; i < producer_count_; i++) {
    for (size_t j = 0; j < consumer_count_; j++) {
      delete producers_[i]->queue_vec[j];
    }
    delete producers_[i];
    producers_[i] = NULL;
  }
  for (size_t j = 0; j < consumer_count_; j++) {
    delete consumers_[j];
    consumers_[j] = NULL;
  }
}

template <typename T>
typename DispatchQueue<T>::OutQueue* DispatchQueue<T>::RegisterProducer() {
  std::lock_guard<std::mutex> lock(mutex_);

  if (producer_count_ >= kMaxProducers)
    return NULL;

  _Producer* producer = new _Producer(this);
  for (size_t i = 0; i < consumer_count_; i++) {
    _Queue* queue = new _Queue(qlen_);
    consumers_[i]->queue_vec[producer_count_] = queue;
    producer->queue_vec[i] = queue;
  }
  producers_[producer_count_] = producer;
  producer_count_++;
  return producer;
}

template <typename T>
typename DispatchQueue<T>::InQueue* DispatchQueue<T>::RegisterConsumer() {
  std::lock_guard<std::mutex> lock(mutex_);

  if (consumer_count_ >= kMaxConsumers)
    return NULL;

  _Consumer* consumer = new _Consumer(this);
  for (size_t i = 0; i < producer_count_; i++) {
    _Queue* queue = new _Queue(qlen_);
    consumer->queue_vec[i] = queue;
    producers_[i]->queue_vec[consumer_count_] = queue;
  }
  consumers_[consumer_count_] = consumer;
  consumer_count_++;
  return consumer;
}

}  // namespace ccb

#endif  // CCBASE_DISPATCH_QUEUE_H_
