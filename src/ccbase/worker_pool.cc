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
#include <mutex>
#include <algorithm>
#include "ccbase/thread.h"
#include "ccbase/worker_pool.h"

namespace ccb {

namespace {

// wait 10 seconds before shrinking workers
constexpr size_t kShrinkWorkersWaitMs = 10000;

size_t HighWatermark(size_t total_workers) {
  // 3/4 total-workers
  return total_workers - total_workers / 4;
}

size_t ExpandingNumber(size_t total_workers, size_t max) {
  // 1/2 (at least 1) total-workers
  return std::min(total_workers / 2 + 1,
      (max > total_workers ? max - total_workers : 0));
}

size_t LowWatermark(size_t total_workers) {
  // 1/4 total-workers
  return total_workers / 4;
}

}  // namespace

thread_local WorkerPool::Worker* WorkerPool::Worker::tls_self_ = nullptr;

WorkerPool::Worker::Worker(WorkerPool* pool, size_t id,
                           std::shared_ptr<Context> context)
    : pool_(pool),
      id_(id),
      context_(context),
      stop_flag_(false) {
  char name[16];
  snprintf(name, sizeof(name), "wp%lu-%lu", pool->id(), id);
  thread_ = CreateThread(name, BindClosure(this, &Worker::WorkerMainEntry));
}

WorkerPool::Worker::~Worker() {
  if (thread_.joinable()) {
    stop_flag_.store(true, std::memory_order_release);
    thread_.join();
  }
}

void WorkerPool::Worker::ExitWithAutoCleanup() {
  thread_.detach();
  on_exit_ = [this] {
    delete this;
  };
  stop_flag_.store(true, std::memory_order_release);
}

void WorkerPool::Worker::WorkerMainEntry() {
  tls_self_ = this;
  ClosureFunc<void()> task_func;
  while (pool_->WorkerPollTask(this, &task_func)) {
    pool_->WorkerBeginProcess(this);
    task_func();
    pool_->WorkerEndProcess(this);
  }
  ClosureFunc<void()> on_exit{std::move(on_exit_)};
  if (on_exit) on_exit();
}


WorkerPool::WorkerPool(size_t min_workers,
                       size_t max_workers,
                       size_t queue_size)
    : WorkerPool(min_workers, max_workers, queue_size, [](size_t) {
        static std::shared_ptr<Context> default_context{new Context};
        return default_context;
      }) {
}

WorkerPool::WorkerPool(size_t min_workers,
                       size_t max_workers,
                       size_t queue_size,
                       ContextSupplier context_supplier)
    : min_workers_(min_workers),
      max_workers_(max_workers),
      total_workers_(0),
      busy_workers_(0),
      next_worker_id_(0),
      last_above_low_watermark_ts_(0),
      timer_wheel_(1000, true),
      sched_timer_task_(BindClosure(this, &WorkerPool::SchedTimerTaskInLock)),
      task_queue_(std::make_shared<TaskQueue>(queue_size)),
      shared_inq_(task_queue_->RegisterConsumer()),
      context_supplier_(context_supplier) {
  ExpandWorkersInLock(min_workers);
  if (total_workers_ < min_workers_) {
    throw std::runtime_error("create minimal workers failed");
  }
}

WorkerPool::~WorkerPool() {
  std::lock_guard<std::mutex> locker(updating_mutex_);
  // signal all worker threads to exit
  for (auto& entry : workers_) {
    entry.second->stop_flag_.store(true, std::memory_order_release);
  }
  workers_.clear();
}

bool WorkerPool::WorkerPollTask(Worker* worker, ClosureFunc<void()>* task) {
  std::lock_guard<std::mutex> lock(polling_mutex_);
  while (!worker->stop_flag_.load(std::memory_order_acquire)) {
    if (PollTimerTaskInLock(task)) {
      return true;
    }
    if (shared_inq_->Pop(task)) {
      return true;
    }
    usleep(1000);
  }
  return shared_inq_->Pop(task);
}

bool WorkerPool::PollTimerTaskInLock(ClosureFunc<void()>* task) {
  timer_wheel_.MoveOn(sched_timer_task_);
  if (timer_task_queue_.empty()) {
    return false;
  } else {
    *task = std::move(timer_task_queue_.front());
    timer_task_queue_.pop();
    return true;
  }
}

void WorkerPool::SchedTimerTaskInLock(ClosureFunc<void()> task) {
  timer_task_queue_.push(std::move(task));
}

void WorkerPool::WorkerBeginProcess(Worker* worker) {
  busy_workers_++;
  if (CheckHighWatermark() && updating_mutex_.try_lock()) {
    if (CheckHighWatermark()) {
      ExpandWorkersInLock(ExpandingNumber(
          total_workers_.load(std::memory_order_relaxed), max_workers_));
    }
    updating_mutex_.unlock();
  }
}

void WorkerPool::WorkerEndProcess(Worker* worker) {
  busy_workers_--;
  if (CheckLowWatermark() && updating_mutex_.try_lock()) {
    if (CheckLowWatermark()) {
      RetireWorkerInLock(worker);
    }
    updating_mutex_.unlock();
  }
}

bool WorkerPool::CheckHighWatermark() {
  if (total_workers_.load(std::memory_order_relaxed) >= max_workers_) {
    return false;
  }
  if (busy_workers_.load(std::memory_order_relaxed)
      >= HighWatermark(total_workers_.load(std::memory_order_relaxed))) {
    last_above_low_watermark_ts_.store(timer_wheel_.GetCurrentTick(),
                                       std::memory_order_relaxed);
    return true;
  } else {
    return false;
  }
}

bool WorkerPool::CheckLowWatermark() {
  if (total_workers_.load(std::memory_order_relaxed) <= min_workers_) {
    return false;
  }
  if (busy_workers_.load(std::memory_order_relaxed)
      <= LowWatermark(total_workers_.load(std::memory_order_relaxed))) {
    if (last_above_low_watermark_ts_.load(std::memory_order_relaxed)
        + kShrinkWorkersWaitMs <= timer_wheel_.GetCurrentTick()) {
      return true;
    } else {
      return false;
    }
  } else {
    last_above_low_watermark_ts_.store(timer_wheel_.GetCurrentTick(),
                                       std::memory_order_relaxed);
    return false;
  }
}

void WorkerPool::ExpandWorkersInLock(size_t num) {
  for (size_t i = 0; i < num; i++) {
    size_t worker_id = next_worker_id_++;
    std::shared_ptr<Context> context = context_supplier_(worker_id);
    if (!context) {
      break;
    }
    workers_[worker_id].reset(new Worker(this, worker_id, std::move(context)));
  }
  total_workers_.store(workers_.size(), std::memory_order_relaxed);
}

void WorkerPool::RetireWorkerInLock(Worker* worker) {
  auto it = workers_.find(worker->id());
  it->second.release()->ExitWithAutoCleanup();
  workers_.erase(it);
  total_workers_.store(workers_.size(), std::memory_order_relaxed);
}

WorkerPool::TaskQueue::OutQueue* WorkerPool::GetOutQueue() {
  auto& client_ctx = tls_client_ctx_.get();
  if (!client_ctx) {
    client_ctx.queue_holder = task_queue_;
    client_ctx.out_queue = task_queue_->RegisterProducer();
  }
  return client_ctx.out_queue;
}

bool WorkerPool::PostTask(ClosureFunc<void()> func) {
  TaskQueue::OutQueue* outq = GetOutQueue();
  return outq->Push(std::move(func));
}

bool WorkerPool::PostTask(ClosureFunc<void()> func, size_t delay_ms) {
  TaskQueue::OutQueue* outq = GetOutQueue();
  return outq->Push([func, delay_ms] {
    Worker::self()->timer_wheel()->AddTimer(delay_ms, std::move(func));
  });
}

bool WorkerPool::PostPeriodTask(ClosureFunc<void()> func, size_t period_ms) {
  TaskQueue::OutQueue* outq = GetOutQueue();
  return outq->Push([func, period_ms] {
    Worker::self()->timer_wheel()->AddPeriodTimer(period_ms, std::move(func));
  });
}

}  // namespace ccb
