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
#include <time.h>
#include <mutex>
#include <atomic>
#include <vector>
#include <utility>
#include <algorithm>
#include "ccbase/timer_wheel.h"
#include "ccbase/macro_list.h"

#define WHEEL_VECS 5
#define TVN_BITS 6
#define TVR_BITS 8
#define TVN_SIZE (1 << TVN_BITS)
#define TVR_SIZE (1 << TVR_BITS)
#define TVN_MASK (TVN_SIZE - 1)
#define TVR_MASK (TVR_SIZE - 1)

namespace ccb {

constexpr int kTimerFlagHasOwner = 0x1;
constexpr int kTimerFlagPeriod = 0x2;

#define TIMER_WHEEL_NODE(head)  static_cast<TimerWheelNode*>(CCB_LIST_ENTRY(head, ListNode, list))

struct ListNode {
  ListHead list;
};

struct TimerWheelNode : ListNode {
  tick_t timeout;
  tick_t expire;
  ClosureFunc<void()> callback;
  uint8_t flags;
};

struct TimerVecRoot {
  int index;
  ListHead vec[TVR_SIZE];
};

struct TimerVec {
  int index;
  ListHead vec[TVN_SIZE];
};

struct TimerWheelVecs {
  TimerVec tv5;
  TimerVec tv4;
  TimerVec tv3;
  TimerVec tv2;
  TimerVecRoot tv1;
  TimerVec * const tvecs[WHEEL_VECS] = {
    reinterpret_cast<TimerVec *>(&tv1), &tv2, &tv3, &tv4, &tv5
  };

  TimerWheelVecs() {
    int i;
    for (i = 0; i < TVN_SIZE; i++) {
      CCB_INIT_LIST_HEAD(tv5.vec + i);
      CCB_INIT_LIST_HEAD(tv4.vec + i);
      CCB_INIT_LIST_HEAD(tv3.vec + i);
      CCB_INIT_LIST_HEAD(tv2.vec + i);
    }
    tv5.index = 0;
    tv4.index = 0;
    tv3.index = 0;
    tv2.index = 0;
    for (i = 0; i < TVR_SIZE; i++) {
      CCB_INIT_LIST_HEAD(tv1.vec + i);
    }
    tv1.index = 0;
  }
};

class TimerWheelImpl : public std::enable_shared_from_this<TimerWheelImpl> {
 public:
  TimerWheelImpl(size_t us_per_tick, bool enable_lock);
  ~TimerWheelImpl();

  bool AddTimer(tick_t timeout,
                ClosureFunc<void()> callback,
                TimerOwner* owner = nullptr);
  bool ResetTimer(const TimerOwner& owner,
                  tick_t timeout);
  bool AddPeriodTimer(tick_t timeout,
                      ClosureFunc<void()> callback,
                      TimerOwner* owner = nullptr);
  bool ResetPeriodTimer(const TimerOwner& owner,
                        tick_t timeout);

  void MoveOn() {
    MoveOn(nullptr);
  }
  void MoveOn(ClosureFunc<void(ClosureFunc<void()>)> sched_func);

  size_t timer_count() const {
    return timer_count_;
  }
  tick_t tick_cur() const {
    return tick_cur_.load(std::memory_order_relaxed);
  }
  // used by TimerOwner
  void DelTimerNode(TimerWheelNode* node);

 private:
  bool AddTimerNode(TimerWheelNode* node);
  bool AddTimerNodeInLock(TimerWheelNode* node);
  void DelTimerNodeInLock(TimerWheelNode* node);
  void CascadeTimers(TimerVec* tv);
  void PollTimerWheel(std::vector<std::pair<TimerWheelNode*,
                                            ClosureFunc<void()>>>* out);
  void InitTick();
  tick_t GetTickNow() const;
  tick_t ToTick(const struct timespec& ts) const;

 private:
  // conditional locker
  class Locker {
   public:
    Locker(std::mutex& m, bool on) : m_(m), on_(on) {  // NOLINT
      if (on_) m_.lock();
    }
    ~Locker() {
      if (on_) m_.unlock();
    }
   private:
    std::mutex& m_;
    bool on_;
  };

 private:
  TimerWheelVecs wheel_;
  std::mutex mutex_;
  size_t us_per_tick_;
  bool enable_lock_;
  size_t timer_count_;
  std::atomic<tick_t> tick_cur_;
  struct timespec ts_start_;
  static thread_local bool tls_tracking_dead_nodes_;
  static thread_local std::vector<TimerWheelNode*> tls_dead_nodes_;
};

thread_local bool TimerWheelImpl::tls_tracking_dead_nodes_{false};
thread_local std::vector<TimerWheelNode*> TimerWheelImpl::tls_dead_nodes_;

TimerWheelImpl::TimerWheelImpl(size_t us_per_tick, bool enable_lock)
    : us_per_tick_(us_per_tick)
    , enable_lock_(enable_lock)
    , timer_count_(0) {
  InitTick();
}

TimerWheelImpl::~TimerWheelImpl() {
  for (size_t tvecs_idx = 0; tvecs_idx < WHEEL_VECS; tvecs_idx++) {
    TimerVec* tv = wheel_.tvecs[tvecs_idx];
    size_t tv_size = (tvecs_idx == 0 ? TVR_SIZE : TVN_SIZE);
    for (size_t tv_index = 0; tv_index < tv_size; tv_index++) {
      for (ListHead *head = tv->vec + tv_index, *curr = head->next;
          curr != head; curr = head->next) {
        TimerWheelNode* node = TIMER_WHEEL_NODE(curr);
        DelTimerNodeInLock(node);
        // if owner alive timer-wheel should never freed
        assert(!(node->flags & kTimerFlagHasOwner));
        delete node;
      }
    }
  }
}

bool TimerWheelImpl::AddTimer(tick_t timeout,
                              ClosureFunc<void()> callback,
                              TimerOwner* owner) {
  TimerWheelNode* node = nullptr;
  if (owner) {
    if (owner->has_timer()) {
      node = owner->timer_.get();
      DelTimerNode(node);
    } else {
      node = new TimerWheelNode;
      owner->timer_.reset(node);
    }
    owner->timer_wheel_ = shared_from_this();
  } else {
    node = new TimerWheelNode;
  }
  node->timeout = timeout;
  node->expire = tick_cur() + timeout;
  node->callback = std::move(callback);
  node->flags = (owner ? kTimerFlagHasOwner : 0);
  return AddTimerNode(node);
}

bool TimerWheelImpl::ResetTimer(const TimerOwner& owner,
                                tick_t timeout) {
  if (!owner.has_timer()) return false;
  TimerWheelNode* node = owner.timer_.get();
  DelTimerNode(node);
  node->timeout = timeout;
  node->expire = tick_cur() + timeout;
  node->flags = kTimerFlagHasOwner;
  return AddTimerNode(node);
}

bool TimerWheelImpl::AddPeriodTimer(tick_t timeout,
                                    ClosureFunc<void()> callback,
                                    TimerOwner* owner) {
  if (timeout == 0) {
    // 0-tick period timer is not allowed
    return false;
  }
  TimerWheelNode* node = nullptr;
  if (owner) {
    if (owner->has_timer()) {
      node = owner->timer_.get();
      DelTimerNode(node);
    } else {
      node = new TimerWheelNode;
      owner->timer_.reset(node);
    }
    owner->timer_wheel_ = shared_from_this();
  } else {
    node = new TimerWheelNode;
  }
  node->timeout = timeout;
  node->expire = tick_cur() + timeout;
  node->callback = std::move(callback);
  node->flags = kTimerFlagPeriod | (owner ? kTimerFlagHasOwner : 0);
  return AddTimerNode(node);
}

bool TimerWheelImpl::ResetPeriodTimer(const TimerOwner& owner,
                                      tick_t timeout) {
  if (timeout == 0) {
    // 0-tick period timer is not allowed
    return false;
  }
  if (!owner.has_timer()) return false;
  TimerWheelNode* node = owner.timer_.get();
  DelTimerNode(node);
  node->timeout = timeout;
  node->expire = tick_cur() + timeout;
  node->flags = kTimerFlagPeriod | kTimerFlagHasOwner;
  return AddTimerNode(node);
}

void TimerWheelImpl::CascadeTimers(TimerVec* tv) {
  /* cascade all the timers from tv up one level */
  ListHead *head;
  ListHead *curr;
  ListHead *next;
  head = tv->vec + tv->index;
  curr = head->next;
  /*
   * We are removing _all_ timers from the list, so we don't have to
   * detach them individually, just clear the list afterwards.
   */
  while (curr != head) {
    TimerWheelNode* node = TIMER_WHEEL_NODE(curr);
    next = curr->next;
    AddTimerNodeInLock(node);
    curr = next;
  }
  CCB_INIT_LIST_HEAD(head);
  tv->index = (tv->index + 1) & TVN_MASK;
}

void TimerWheelImpl::MoveOn(ClosureFunc<void(ClosureFunc<void()>)> sched_func) {
  thread_local std::vector<std::pair<TimerWheelNode*,
                                     ClosureFunc<void()>>> cb_vec;
  PollTimerWheel(&cb_vec);
  tls_tracking_dead_nodes_ = true;
  // run callback without lock
  for (auto& cb : cb_vec) {
    TimerWheelNode* node = cb.first;
    ClosureFunc<void()>& callback = cb.second;
    if (node && !tls_dead_nodes_.empty() &&
        std::find(tls_dead_nodes_.begin(), tls_dead_nodes_.end(), node)
               != tls_dead_nodes_.end()) {
      // the timer has been deleted by previous callbacks
      continue;
    }
    if (callback) {
      if (!sched_func) {
        callback();
      } else {
        sched_func(std::move(callback));
      }
    }
  }
  tls_tracking_dead_nodes_ = false;
  tls_dead_nodes_.clear();
  cb_vec.clear();
}

void TimerWheelImpl::PollTimerWheel(
    std::vector<std::pair<TimerWheelNode*, ClosureFunc<void()>>>* out) {
  Locker lock(mutex_, enable_lock_);

  tick_t tick_to = GetTickNow();
  if (tick_to < tick_cur()) {
    return;
  }

  auto& tv1 = wheel_.tv1;
  auto& tvecs = wheel_.tvecs;
  while (tick_to >= tick_cur()) {
    if (tv1.index == 0) {
      int n = 1;
      do {
        CascadeTimers(tvecs[n]);
      } while (tvecs[n]->index == 1 && ++n < WHEEL_VECS);
    }

    for (ListHead *head = tv1.vec + tv1.index, *curr = head->next;
         curr != head; curr = head->next) {
      TimerWheelNode* node = TIMER_WHEEL_NODE(curr);
      DelTimerNodeInLock(node);
      if (node->flags & kTimerFlagPeriod) {  // period timer
        // copy the callback closure
        out->emplace_back(node, node->callback);
        // reschedule
        node->expire = tick_cur() + node->timeout;
        AddTimerNodeInLock(node);
      } else {  // oneshot timer
        if (!(node->flags & kTimerFlagHasOwner)) {
          // no owner, move the callback closure
          out->emplace_back(nullptr, std::move(node->callback));
          delete node;
        } else {
          // has owner, copy the callback closure
          out->emplace_back(node, node->callback);
        }
      }
    }
    // next tick
    tick_cur_.store(tick_cur() + 1, std::memory_order_relaxed);
    tv1.index = (tv1.index + 1) & TVR_MASK;
  }
}

inline bool TimerWheelImpl::AddTimerNode(TimerWheelNode* node) {
  Locker lock(mutex_, enable_lock_);
  return AddTimerNodeInLock(node);
}

bool TimerWheelImpl::AddTimerNodeInLock(TimerWheelNode* node) {
  // link the node
    tick_t tick_exp = node->expire;
    tick_t idx = tick_exp - tick_cur();
    ListHead* vec;
    if (idx < TVR_SIZE) {
        int i = static_cast<int>(tick_exp & TVR_MASK);
        vec = wheel_.tv1.vec + i;
    } else if (idx < (tick_t)1 << (TVR_BITS + TVN_BITS)) {
        int i = static_cast<int>((tick_exp >> TVR_BITS) & TVN_MASK);
        vec = wheel_.tv2.vec + i;
    } else if (idx < (tick_t)1 << (TVR_BITS + 2 * TVN_BITS)) {
        int i = static_cast<int>((tick_exp >> (TVR_BITS + TVN_BITS)) & TVN_MASK);
        vec = wheel_.tv3.vec + i;
    } else if (idx < (tick_t)1 << (TVR_BITS + 3 * TVN_BITS)) {
        int i = static_cast<int>((tick_exp >> (TVR_BITS + 2 * TVN_BITS)) & TVN_MASK);
        vec = wheel_.tv4.vec + i;
    } else if (idx < (tick_t)1 << (TVR_BITS + 4 * TVN_BITS)) {
        int i = static_cast<int>((tick_exp >> (TVR_BITS + 3 * TVN_BITS)) & TVN_MASK);
        vec = wheel_.tv5.vec + i;
    } else {
    return false;
  }
  CCB_LIST_ADD(&(node->list), vec->prev);
  timer_count_++;
  return true;
}

inline void TimerWheelImpl::DelTimerNode(TimerWheelNode* node) {
  if (tls_tracking_dead_nodes_) {
    tls_dead_nodes_.push_back(node);
  }
  Locker lock(mutex_, enable_lock_);
  DelTimerNodeInLock(node);
}

inline void TimerWheelImpl::DelTimerNodeInLock(TimerWheelNode* node) {
  // unlink the node
  if (!CCB_LIST_EMPTY(&(node->list))) {
    CCB_LIST_DEL_INIT(&(node->list));
    timer_count_--;
  }
}

void TimerWheelImpl::InitTick() {
  if (clock_gettime(CLOCK_MONOTONIC, &ts_start_) < 0) {
    throw std::system_error(errno, std::system_category(), "clock_gettime");
  }
  tick_cur_.store(0, std::memory_order_relaxed);
}

tick_t TimerWheelImpl::GetTickNow() const {
  struct timespec ts;
  if (clock_gettime(CLOCK_MONOTONIC, &ts) < 0) {
    throw std::system_error(errno, std::system_category(), "clock_gettime");
  }
  return ToTick(ts);
}

tick_t TimerWheelImpl::ToTick(const struct timespec& ts) const {
  struct timespec dur;
  if (ts.tv_nsec >= ts_start_.tv_nsec) {
    dur.tv_nsec = ts.tv_nsec - ts_start_.tv_nsec;
    dur.tv_sec = ts.tv_sec - ts_start_.tv_sec;
  } else {
    dur.tv_nsec = ts.tv_nsec + 1000000000 - ts_start_.tv_nsec;
    dur.tv_sec = ts.tv_sec - 1 - ts_start_.tv_sec;
  }
  if (dur.tv_sec < 0) {
    throw std::runtime_error("ToTick: invalid timespec value");
  }
  tick_t us = (static_cast<tick_t>(dur.tv_sec) * 1000000 + dur.tv_nsec / 1000);
  return us / us_per_tick_;
}


TimerOwner::TimerOwner() {
}

TimerOwner::~TimerOwner() {
  Cancel();
}

void TimerOwner::Cancel() {
  if (has_timer()) {
    timer_wheel_->DelTimerNode(timer_.get());
  }
}


TimerWheel::TimerWheel(size_t us_per_tick, bool enable_lock_for_mt)
  : pimpl_(std::make_shared<TimerWheelImpl>(us_per_tick, enable_lock_for_mt)) {
}

TimerWheel::~TimerWheel() {
}

bool TimerWheel::AddTimer(tick_t timeout,
                          ClosureFunc<void()> callback,
                          TimerOwner* owner) {
  return pimpl_->AddTimer(timeout, std::move(callback), owner);
}

bool TimerWheel::ResetTimer(const TimerOwner& owner,
                            tick_t timeout) {
  return pimpl_->ResetTimer(owner, timeout);
}

bool TimerWheel::AddPeriodTimer(tick_t timeout,
                                ClosureFunc<void()> callback,
                                TimerOwner* owner) {
  return pimpl_->AddPeriodTimer(timeout, std::move(callback), owner);
}

bool TimerWheel::ResetPeriodTimer(const TimerOwner& owner,
                                  tick_t timeout) {
  return pimpl_->ResetPeriodTimer(owner, timeout);
}

void TimerWheel::MoveOn() {
  return pimpl_->MoveOn();
}

void TimerWheel::MoveOn(ClosureFunc<void(ClosureFunc<void()>)> sched_func) {
  return pimpl_->MoveOn(std::move(sched_func));
}

size_t TimerWheel::GetTimerCount() const {
  return pimpl_->timer_count();
}

size_t TimerWheel::GetCurrentTick() const {
  return pimpl_->tick_cur();
}

}  // namespace ccb

