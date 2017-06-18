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
#include <atomic>
#include <vector>
#include <utility>
#include "ccbase/fast_queue.h"

namespace ccb {

template <class T, size_t kMaxProducers = 1024,
                   size_t kMaxConsumers = 1024>
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

  using Queue = FastQueue<T, false>;
  class Producer;
  class Consumer;

  size_t qlen_;
  std::mutex mutex_;
  std::atomic<Producer*> producers_[kMaxProducers];
  std::atomic<Consumer*> consumers_[kMaxConsumers];
  std::atomic<size_t> producer_count_;
  std::atomic<size_t> consumer_count_;
};

///////////////////////////////////////////////////////////////////

template <class T, size_t kMaxProducers, size_t kMaxConsumers>
class DispatchQueue<T, kMaxProducers, kMaxConsumers>::Producer
    : public DispatchQueue<T, kMaxProducers, kMaxConsumers>::OutQueue {
 public:
  Producer(DispatchQueue<T, kMaxProducers, kMaxConsumers>* dq)
      : dispatch_queue_(dq), cur_index_(-1U) {
    for (auto& ap : queue_vec)
      // std::atomic_init is not available in gcc-4.9
      ap.store(nullptr, std::memory_order_relaxed);
  }
  bool Push(const T& val) override;
  bool Push(T&& val) override;
  bool Push(size_t idx, const T& val) override;
  bool Push(size_t idx, T&& val) override;

  std::atomic<Queue*> queue_vec[kMaxConsumers];

 private:
  DispatchQueue<T, kMaxProducers, kMaxConsumers>* dispatch_queue_;
  size_t cur_index_;
};

template <class T, size_t kMaxProducers, size_t kMaxConsumers>
bool DispatchQueue<T, kMaxProducers, kMaxConsumers>
    ::Producer::Push(const T& val) {
  size_t last_index = cur_index_;
  for (cur_index_++; cur_index_ < kMaxConsumers; cur_index_++) {
    Queue* qptr = queue_vec[cur_index_].load(std::memory_order_acquire);
    if (qptr == nullptr)
      break;
    if (qptr->Push(val))
      return true;
  }
  for (cur_index_ = 0; cur_index_ <= last_index; cur_index_++) {
    Queue* qptr = queue_vec[cur_index_].load(std::memory_order_acquire);
    if (qptr == nullptr)
      break;
    if (qptr->Push(val))
      return true;
  }
  return false;
}

template <class T, size_t kMaxProducers, size_t kMaxConsumers>
bool DispatchQueue<T, kMaxProducers, kMaxConsumers>
    ::Producer::Push(T&& val) {
  size_t last_index = cur_index_;
  for (cur_index_++; cur_index_ < kMaxConsumers; cur_index_++) {
    Queue* qptr = queue_vec[cur_index_].load(std::memory_order_acquire);
    if (qptr == nullptr)
      break;
    if (qptr->Push(std::move(val)))
      return true;
  }
  for (cur_index_ = 0; cur_index_ <= last_index; cur_index_++) {
    Queue* qptr = queue_vec[cur_index_].load(std::memory_order_acquire);
    if (qptr == nullptr)
      break;
    if (qptr->Push(std::move(val)))
      return true;
  }
  return false;
}

template <class T, size_t kMaxProducers, size_t kMaxConsumers>
bool DispatchQueue<T, kMaxProducers, kMaxConsumers>
    ::Producer::Push(size_t idx, const T& val) {
  if (idx < kMaxConsumers) {
    Queue* qptr = queue_vec[idx].load(std::memory_order_acquire);
    if (qptr && qptr->Push(val))
        return true;
  }
  return false;
}

template <class T, size_t kMaxProducers, size_t kMaxConsumers>
bool DispatchQueue<T, kMaxProducers, kMaxConsumers>
    ::Producer::Push(size_t idx, T&& val) {
  if (idx < kMaxConsumers) {
    Queue* qptr = queue_vec[idx].load(std::memory_order_acquire);
    if (qptr && qptr->Push(std::move(val)))
        return true;
  }
  return false;
}

///////////////////////////////////////////////////////////////////

template <class T, size_t kMaxProducers, size_t kMaxConsumers>
class DispatchQueue<T, kMaxProducers, kMaxConsumers>::Consumer
    : public DispatchQueue<T, kMaxProducers, kMaxConsumers>::InQueue {
 public:
  Consumer(DispatchQueue<T, kMaxProducers, kMaxConsumers>* dq)
      : dispatch_queue_(dq), cur_index_(-1U),
        cur_index_read_cnt_(0) {
    for (auto& ap : queue_vec)
      // std::atomic_init is not available in gcc-4.9
      ap.store(nullptr, std::memory_order_relaxed);
  }
  bool Pop(T* ptr) override;
  bool PopWait(T* ptr, int timeout) override;

  std::atomic<Queue*> queue_vec[kMaxProducers];

 private:
  static constexpr size_t kMaxStickyReadCnt = 32;
  DispatchQueue<T, kMaxProducers, kMaxConsumers>* dispatch_queue_;
  size_t cur_index_;
  size_t cur_index_read_cnt_;
};

template <class T, size_t kMaxProducers, size_t kMaxConsumers>
bool DispatchQueue<T, kMaxProducers, kMaxConsumers>
    ::Consumer::Pop(T* ptr) {
  // sticky read for performance
  if (cur_index_read_cnt_ && cur_index_read_cnt_ < kMaxStickyReadCnt) {
    Queue* qptr = queue_vec[cur_index_].load(std::memory_order_acquire);
    if (qptr->Pop(ptr)) {
      cur_index_read_cnt_++;
      return true;
    }
  }
  cur_index_read_cnt_ = 0;

  size_t last_index = cur_index_;
  for (cur_index_++; cur_index_ < kMaxProducers; cur_index_++) {
    Queue* qptr = queue_vec[cur_index_].load(std::memory_order_acquire);
    if (qptr == nullptr)
      break;
    if (qptr->Pop(ptr)) {
      cur_index_read_cnt_ = 1;
      return true;
    }
  }
  for (cur_index_ = 0; cur_index_ <= last_index; cur_index_++) {
    Queue* qptr = queue_vec[cur_index_].load(std::memory_order_acquire);
    if (qptr == nullptr)
      break;
    if (qptr->Pop(ptr)) {
      cur_index_read_cnt_ = 1;
      return true;
    }
  }
  return false;
}

template <class T, size_t kMaxProducers, size_t kMaxConsumers>
bool DispatchQueue<T, kMaxProducers, kMaxConsumers>
    ::Consumer::PopWait(T* ptr, int timeout) {
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

template <class T, size_t kMaxProducers, size_t kMaxConsumers>
DispatchQueue<T, kMaxProducers, kMaxConsumers>::DispatchQueue(size_t qlen)
    : qlen_(qlen), producer_count_(0), consumer_count_(0) {
  // std::atomic_init is not available in gcc-4.9
  for (auto& pr : producers_)
    pr.store(nullptr, std::memory_order_relaxed);
  for (auto& co : consumers_)
    co.store(nullptr, std::memory_order_relaxed);
}

template <class T, size_t kMaxProducers, size_t kMaxConsumers>
DispatchQueue<T, kMaxProducers, kMaxConsumers>::~DispatchQueue() {
  for (size_t i = 0; i < producer_count_.load(); i++) {
    for (size_t j = 0; j < consumer_count_.load(); j++) {
      delete producers_[i].load()->queue_vec[j];
    }
    delete producers_[i].load();
    producers_[i].store(nullptr);
  }
  for (size_t j = 0; j < consumer_count_.load(); j++) {
    delete consumers_[j].load();
    consumers_[j].store(nullptr);
  }
}

template <class T, size_t kMaxProducers, size_t kMaxConsumers>
typename DispatchQueue<T, kMaxProducers, kMaxConsumers>::OutQueue*
DispatchQueue<T, kMaxProducers, kMaxConsumers>::RegisterProducer() {
  std::lock_guard<std::mutex> lock(mutex_);

  size_t producer_count = producer_count_.load(std::memory_order_relaxed);
  if (producer_count >= kMaxProducers)
    return nullptr;

  Producer* producer = new Producer(this);
  for (size_t i = 0; i < consumer_count_.load(std::memory_order_relaxed); i++) {
    Consumer* consumer = consumers_[i].load(std::memory_order_relaxed);
    Queue* queue = new Queue(qlen_);
    consumer->queue_vec[producer_count] = queue;
    producer->queue_vec[i] = queue;
  }
  producers_[producer_count].store(producer, std::memory_order_release);
  producer_count_.store(producer_count + 1, std::memory_order_release);
  return producer;
}

template <class T, size_t kMaxProducers, size_t kMaxConsumers>
typename DispatchQueue<T, kMaxProducers, kMaxConsumers>::InQueue*
DispatchQueue<T, kMaxProducers, kMaxConsumers>::RegisterConsumer() {
  std::lock_guard<std::mutex> lock(mutex_);

  size_t consumer_count = consumer_count_.load(std::memory_order_relaxed);
  if (consumer_count >= kMaxConsumers)
    return nullptr;

  Consumer* consumer = new Consumer(this);
  for (size_t i = 0; i < producer_count_.load(std::memory_order_relaxed); i++) {
    Producer* producer = producers_[i].load(std::memory_order_relaxed);
    Queue* queue = new Queue(qlen_);
    consumer->queue_vec[i] = queue;
    producer->queue_vec[consumer_count] = queue;
  }
  consumers_[consumer_count].store(consumer, std::memory_order_release);
  consumer_count_.store(consumer_count + 1, std::memory_order_release);
  return consumer;
}

}  // namespace ccb

#endif  // CCBASE_DISPATCH_QUEUE_H_
