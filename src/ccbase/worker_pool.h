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
#ifndef CCBASE_WORKER_POOL_H_
#define CCBASE_WORKER_POOL_H_

#include <thread>
#include <atomic>
#include <memory>
#include <map>
#include <queue>
#include "ccbase/common.h"
#include "ccbase/closure.h"
#include "ccbase/timer_wheel.h"
#include "ccbase/dispatch_queue.h"
#include "ccbase/thread_local_obj.h"

namespace ccb {

class WorkerPool {
 public:
  class Context {};
  // For low schedule latency ContextSupplier should be noblocking and do
  // blocking initialization lazily. If null context is returned the worker
  // thread creation will fail.
  using ContextSupplier = ClosureFunc<std::shared_ptr<Context>(size_t worker_id)>;

  class Worker {
   public:
    ~Worker();
    static Worker* self() {
      return tls_self_;
    }
    size_t id() const {
      return id_;
    }
    template <class ContextType>
    ContextType* context() const {
      return static_cast<ContextType*>(context_.get());
    }
    WorkerPool* worker_pool() const {
      return pool_;
    }
    TimerWheel* timer_wheel() const {
      return &pool_->timer_wheel_;
    }

   private:
    CCB_NOT_COPYABLE_AND_MOVABLE(Worker);

    Worker(WorkerPool* pool, size_t id, std::shared_ptr<Context> context);
    void ExitWithAutoCleanup();
    void WorkerMainEntry();

    WorkerPool* pool_;
    size_t id_;
    std::shared_ptr<Context> context_;
    std::atomic_bool stop_flag_;
    ClosureFunc<void()> on_exit_;
    std::thread thread_;
    static thread_local Worker* tls_self_;
    friend class WorkerPool;
  };

 public:
  WorkerPool(size_t min_workers, size_t max_workers, size_t queue_size);
  WorkerPool(size_t min_workers, size_t max_workers, size_t queue_size,
             ContextSupplier context_supplier);
  ~WorkerPool();

  size_t id() const {
    return tls_client_ctx_.instance_id();
  }
  size_t size() const {
    return workers_.size();
  }
  size_t concurrent_workers() const {
    return busy_workers_.load(std::memory_order_relaxed);
  }
  bool is_current_thread() {
    Worker* worker = Worker::self();
    return (worker && worker->pool_ == this);
  }

  bool PostTask(ClosureFunc<void()> func);
  bool PostTask(ClosureFunc<void()> func, size_t delay_ms);
  bool PostPeriodTask(ClosureFunc<void()> func, size_t period_ms);

 private:
  CCB_NOT_COPYABLE_AND_MOVABLE(WorkerPool);

  using TaskQueue = DispatchQueue<ClosureFunc<void()>>;

  bool WorkerPollTask(ClosureFunc<void()>* task);
  bool PollTimerTaskInLock(ClosureFunc<void()>* task);
  void SchedTimerTaskInLock(ClosureFunc<void()> task);
  void WorkerBeginProcess(Worker* worker);
  void WorkerEndProcess(Worker* worker);
  bool CheckHighWatermark();
  bool CheckLowWatermark();
  void ExpandWorkersInLock(size_t num);
  void RetireWorkerInLock(Worker* worker);
  TaskQueue::OutQueue* GetOutQueue();

  struct ClientContext {
    std::shared_ptr<TaskQueue> queue_holder;
    TaskQueue::OutQueue* out_queue;

    ClientContext()
        : queue_holder(nullptr), out_queue(nullptr) {}
    ~ClientContext() {
      if (out_queue) {
        out_queue->Unregister();
      }
    }
    operator bool() const {
      return out_queue;
    }
  };

  const size_t min_workers_;
  const size_t max_workers_;
  std::atomic<size_t> total_workers_;
  std::atomic<size_t> busy_workers_;
  std::atomic<size_t> next_worker_id_;
  std::atomic<tick_t> last_above_low_watermark_ts_;
  TimerWheel timer_wheel_;
  std::queue<ClosureFunc<void()>> timer_task_queue_;
  ClosureFunc<void(ClosureFunc<void()>)> sched_timer_task_;
  std::shared_ptr<TaskQueue> task_queue_;
  TaskQueue::InQueue* shared_inq_;
  ContextSupplier context_supplier_;
  std::map<size_t, std::unique_ptr<Worker>> workers_;
  std::mutex updating_mutex_;
  std::timed_mutex polling_mutex_;
  ThreadLocalObj<ClientContext> tls_client_ctx_;
};

}  // namespace ccb

#endif  // CCBASE_WORKER_POOL_H_
