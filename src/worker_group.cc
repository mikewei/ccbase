#include <stdio.h>
#include "ccbase/thread.h"
#include "ccbase/worker_group.h"

namespace ccb {

thread_local Worker* Worker::tls_self = nullptr;

Worker::Worker(WorkerGroup* grp, size_t id, TaskQueue::InQueue* q)
  : TimerWheel(1000, false)
  , group_(grp)
  , id_(id)
  , inq_(q)
  , stop_flag_(false)
{
  char name[16];
  snprintf(name, sizeof(name), "w%lu-%lu", grp->id(), id);
  thread_ = CreateThread(name, std::bind(&Worker::WorkerMainEntry, this));
}

Worker::~Worker()
{
  stop_flag_ = true;
  thread_.join();
}

bool Worker::PostTask(ClosureFunc<void()> func)
{
  return group_->PostTask(id_, std::move(func));
}

void Worker::WorkerMainEntry()
{
  tls_self = this;
  while (!stop_flag_) {
    TimerWheel::MoveOn();
    BatchProcessTasks(50);
  }
}

void Worker::BatchProcessTasks(size_t max)
{
  for (size_t cnt = 0; cnt < max ; cnt++) {
    ClosureFunc<void()> func;
    if (!inq_->Pop(&func)) {
      if (inq_->PopWait(&func, 1)) func();
      break;
    } 
    func();
  }
}


thread_local std::unordered_map<size_t, TaskQueue::OutQueue*> WorkerGroup::tls_producer_ctx{100};
thread_local std::array<TaskQueue::OutQueue*, 64> WorkerGroup::tls_producer_ctx_cache_;

std::atomic<size_t> WorkerGroup::s_next_group_id(0);

WorkerGroup::WorkerGroup(size_t worker_num, size_t queue_size)
  : queue_(queue_size)
{
  group_id_ = s_next_group_id.fetch_add(1);
  for (size_t id = 0; id < worker_num; id++) {
    workers_.emplace_back(new Worker(this, id, queue_.RegisterConsumer()));
  }
}

WorkerGroup::~WorkerGroup()
{
}


TaskQueue::OutQueue* WorkerGroup::GetOutQueue()
{
  auto& outq = group_id_ < tls_producer_ctx_cache_.size()
                ? tls_producer_ctx_cache_[group_id_]
                : tls_producer_ctx[group_id_];
  if (!outq) {
    outq = queue_.RegisterProducer();
  }
  return outq;
}

bool WorkerGroup::PostTask(ClosureFunc<void()> func)
{
  TaskQueue::OutQueue* outq = GetOutQueue();
  return outq->Push(std::move(func));
}

bool WorkerGroup::PostTask(size_t worker_id, ClosureFunc<void()> func)
{
  TaskQueue::OutQueue* outq = GetOutQueue();
  return outq->Push(worker_id, std::move(func));
}

bool WorkerGroup::PostTask(ClosureFunc<void()> func, size_t delay_ms)
{
  TaskQueue::OutQueue* outq = GetOutQueue();
  return outq->Push([func, delay_ms] {
    Worker::self()->AddTimer(delay_ms, std::move(func));
  });
}

bool WorkerGroup::PostTask(size_t worker_id, ClosureFunc<void()> func, size_t delay_ms)
{
  TaskQueue::OutQueue* outq = GetOutQueue();
  return outq->Push(worker_id, [func, delay_ms] {
    Worker::self()->AddTimer(delay_ms, std::move(func));
  });
}

bool WorkerGroup::PostPeriodTask(ClosureFunc<void()> func, size_t period_ms)
{
  TaskQueue::OutQueue* outq = GetOutQueue();
  return outq->Push([func, period_ms] {
    Worker::self()->AddPeriodTimer(period_ms, std::move(func));
  });
}

bool WorkerGroup::PostPeriodTask(size_t worker_id, ClosureFunc<void()> func, size_t period_ms)
{
  TaskQueue::OutQueue* outq = GetOutQueue();
  return outq->Push(worker_id, [func, period_ms] {
    Worker::self()->AddPeriodTimer(period_ms, std::move(func));
  });
}

} // namespace ccb
