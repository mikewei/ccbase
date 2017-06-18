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
#include <stdio.h>
#include <utility>
#include "ccbase/thread.h"
#include "ccbase/worker_group.h"

namespace ccb {

thread_local Worker* Worker::tls_self_ = nullptr;

Worker::Worker(WorkerGroup* grp, size_t id, TaskQueue::InQueue* q)
    : TimerWheel(1000, false),
      group_(grp),
      id_(id),
      inq_(q),
      stop_flag_(false) {
  char name[16];
  snprintf(name, sizeof(name), "w%lu-%lu", grp->id(), id);
  thread_ = CreateThread(name, std::bind(&Worker::WorkerMainEntry, this));
}

Worker::~Worker() {
  stop_flag_ = true;
  thread_.join();
}

bool Worker::PostTask(ClosureFunc<void()> func) {
  return group_->PostTask(id_, std::move(func));
}

void Worker::WorkerMainEntry() {
  tls_self_ = this;
  while (!stop_flag_) {
    TimerWheel::MoveOn();
    BatchProcessTasks(50);
  }
}

void Worker::BatchProcessTasks(size_t max) {
  for (size_t cnt = 0; cnt < max ; cnt++) {
    ClosureFunc<void()> func;
    if (!inq_->Pop(&func)) {
      if (inq_->PopWait(&func, 1)) func();
      break;
    }
    func();
  }
}


thread_local std::unordered_map<size_t, WorkerGroup::QHolder>
WorkerGroup::tls_producer_ctx_{100};

thread_local std::array<WorkerGroup::QHolder, 64>
WorkerGroup::tls_producer_ctx_cache_;

std::atomic<size_t> WorkerGroup::s_next_group_id_{0};

WorkerGroup::WorkerGroup(size_t worker_num, size_t queue_size)
    : queue_(queue_size) {
  group_id_ = s_next_group_id_.fetch_add(1);
  for (size_t id = 0; id < worker_num; id++) {
    workers_.emplace_back(new Worker(this, id, queue_.RegisterConsumer()));
  }
}

WorkerGroup::~WorkerGroup() {
}


TaskQueue::OutQueue* WorkerGroup::GetOutQueue() {
  auto& outq = group_id_ < tls_producer_ctx_cache_.size()
                ? tls_producer_ctx_cache_[group_id_]
                : tls_producer_ctx_[group_id_];
  if (!outq) {
    outq.reset(queue_.RegisterProducer());
  }
  return outq.get();
}

bool WorkerGroup::PostTask(ClosureFunc<void()> func) {
  TaskQueue::OutQueue* outq = GetOutQueue();
  return outq->Push(std::move(func));
}

bool WorkerGroup::PostTask(size_t worker_id, ClosureFunc<void()> func) {
  TaskQueue::OutQueue* outq = GetOutQueue();
  return outq->Push(worker_id, std::move(func));
}

bool WorkerGroup::PostTask(ClosureFunc<void()> func, size_t delay_ms) {
  TaskQueue::OutQueue* outq = GetOutQueue();
  return outq->Push([func, delay_ms] {
    Worker::self()->AddTimer(delay_ms, std::move(func));
  });
}

bool WorkerGroup::PostTask(size_t worker_id, ClosureFunc<void()> func,
                           size_t delay_ms) {
  TaskQueue::OutQueue* outq = GetOutQueue();
  return outq->Push(worker_id, [func, delay_ms] {
    Worker::self()->AddTimer(delay_ms, std::move(func));
  });
}

bool WorkerGroup::PostPeriodTask(ClosureFunc<void()> func, size_t period_ms) {
  TaskQueue::OutQueue* outq = GetOutQueue();
  return outq->Push([func, period_ms] {
    Worker::self()->AddPeriodTimer(period_ms, std::move(func));
  });
}

bool WorkerGroup::PostPeriodTask(size_t worker_id, ClosureFunc<void()> func,
                                 size_t period_ms) {
  TaskQueue::OutQueue* outq = GetOutQueue();
  return outq->Push(worker_id, [func, period_ms] {
    Worker::self()->AddPeriodTimer(period_ms, std::move(func));
  });
}

}  // namespace ccb
