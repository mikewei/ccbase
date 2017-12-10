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
#include <limits>
#include <utility>
#include "ccbase/thread.h"
#include "ccbase/worker_group.h"

namespace ccb {

namespace {

constexpr size_t kMaxBatchProcessTasks = 16;
constexpr size_t kPollerTimeoutMs = 1;

class DefaultWorkerPoller : public WorkerGroup::Poller {
 public:
  virtual ~DefaultWorkerPoller() {}
  void Poll(size_t timeout_ms) override {
    if (timeout_ms > 0) {
      usleep(timeout_ms * 1000);
    }
  }
  static std::shared_ptr<DefaultWorkerPoller> Instance() {
    static std::shared_ptr<DefaultWorkerPoller> instance_{
      new DefaultWorkerPoller
    };
    return instance_;
  }
};

}  // namespace

thread_local WorkerGroup::Worker* WorkerGroup::Worker::tls_self_ = nullptr;

WorkerGroup::Worker::Worker(WorkerGroup* grp, size_t id,
                            WorkerGroup::TaskQueue::InQueue* q,
                            std::shared_ptr<WorkerGroup::Poller> poller)
    : TimerWheel(1000, false),
      group_(grp),
      id_(id),
      inq_(q),
      poller_(std::move(poller)),
      stop_flag_(false) {
  char name[16];
  snprintf(name, sizeof(name), "w%lu-%lu", grp->id(), id);
  thread_ = CreateThread(name, BindClosure(this, &Worker::WorkerMainEntry));
}

WorkerGroup::Worker::~Worker() {
  stop_flag_.store(true, std::memory_order_release);
  thread_.join();
}

bool WorkerGroup::Worker::PostTask(ClosureFunc<void()> func) {
  return group_->PostTask(id_, std::move(func));
}

void WorkerGroup::Worker::WorkerMainEntry() {
  tls_self_ = this;
  while (!stop_flag_.load(std::memory_order_acquire)) {
    TimerWheel::MoveOn();
    size_t n = BatchProcessTasks(kMaxBatchProcessTasks);
    poller_->Poll(n < kMaxBatchProcessTasks ? kPollerTimeoutMs : 0);
  }
  BatchProcessTasks(std::numeric_limits<size_t>::max());
}

size_t WorkerGroup::Worker::BatchProcessTasks(size_t max) {
  size_t cnt;
  for (cnt = 0; cnt < max ; cnt++) {
    ClosureFunc<void()> func;
    if (!inq_->Pop(&func)) {
      break;
    }
    func();
  }
  return cnt;
}


WorkerGroup::WorkerGroup(size_t worker_num, size_t queue_size)
    : WorkerGroup(worker_num, queue_size, [](size_t) {
        return DefaultWorkerPoller::Instance();
      }) {
}

WorkerGroup::WorkerGroup(size_t worker_num, size_t queue_size,
                         PollerSupplier poller_supplier)
    : queue_(std::make_shared<TaskQueue>(queue_size)) {
  for (size_t id = 0; id < worker_num; id++) {
    workers_.emplace_back(new Worker(this, id, queue_->RegisterConsumer(),
                                     poller_supplier(id)));
  }
}

WorkerGroup::~WorkerGroup() {
}


WorkerGroup::TaskQueue::OutQueue* WorkerGroup::GetOutQueue() {
  auto& client_ctx = tls_client_ctx_.get();
  if (!client_ctx) {
    client_ctx.queue_holder = queue_;
    client_ctx.out_queue = queue_->RegisterProducer();
  }
  return client_ctx.out_queue;
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
