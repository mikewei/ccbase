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
#ifndef CCBASE_WORKER_GROUP_H_
#define CCBASE_WORKER_GROUP_H_

#include <thread>
#include <atomic>
#include <memory>
#include <unordered_map>
#include <array>
#include <vector>
#include "ccbase/common.h"
#include "ccbase/closure.h"
#include "ccbase/timer_wheel.h"
#include "ccbase/dispatch_queue.h"

namespace ccb {

using TaskQueue = DispatchQueue<ClosureFunc<void()>>;

class WorkerGroup;

class Worker : public TimerWheel {
 public:
  ~Worker();
  static Worker* self() {
    return tls_self;
  }
  template <class T> static T& tls() {
    static thread_local T tls_ctx;
    return tls_ctx;
  }
  size_t id() const {
    return id_;
  }
  WorkerGroup* worker_group() const {
    return group_;
  }
  bool PostTask(ClosureFunc<void()> func);

 private:
  NOT_COPYABLE_AND_MOVABLE(Worker);

  Worker(WorkerGroup* grp, size_t id, TaskQueue::InQueue* q);
  void WorkerMainEntry();
  void BatchProcessTasks(size_t max);

  WorkerGroup* group_;
  size_t id_;
  TaskQueue::InQueue* inq_;
  std::atomic_bool stop_flag_;
  std::thread thread_;
  static thread_local Worker* tls_self;
  friend class WorkerGroup;
};

class WorkerGroup {
 public:
  WorkerGroup(size_t worker_num, size_t queue_size);
  ~WorkerGroup();
  size_t id() const {
    return group_id_;
  }
  size_t size() const {
    return workers_.size();
  }
  size_t GroupSize() const {
    return size();
  }
  bool is_current_thread() {
    Worker* worker = Worker::self();
    return (worker && worker->group_ == this);
  }
  bool is_current_thread(size_t worker_id) {
    Worker* worker = Worker::self();
    return (worker && worker->group_ == this && worker->id() == worker_id);
  }

  bool PostTask(ClosureFunc<void()> func);
  bool PostTask(size_t worker_id, ClosureFunc<void()> func);
  bool PostTask(ClosureFunc<void()> func, size_t delay_ms);
  bool PostTask(size_t worker_id, ClosureFunc<void()> func, size_t delay_ms);
  bool PostPeriodTask(ClosureFunc<void()> func, size_t period_ms);
  bool PostPeriodTask(size_t worker_id, ClosureFunc<void()> func,
                      size_t period_ms);

 private:
  TaskQueue::OutQueue* GetOutQueue();

  NOT_COPYABLE_AND_MOVABLE(WorkerGroup);

  TaskQueue queue_;
  std::vector<std::unique_ptr<Worker>> workers_;
  static thread_local std::unordered_map<size_t, TaskQueue::OutQueue*> tls_producer_ctx;
  static thread_local std::array<TaskQueue::OutQueue*, 64> tls_producer_ctx_cache_;
  size_t group_id_;
  static std::atomic<size_t> s_next_group_id;
};

}  // namespace ccb

#endif  // CCBASE_WORKER_GROUP_H_
