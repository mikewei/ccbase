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
#ifndef CCBASE_TIMER_WHEEL_H_
#define CCBASE_TIMER_WHEEL_H_

#include <memory>
#include "ccbase/closure.h"
#include "ccbase/common.h"

namespace ccb {

class TimerWheelImpl;
class TimerWheelNode;
class TimerOwner;

using tick_t = uint64_t;

class TimerWheel {
 public:
  explicit TimerWheel(size_t us_per_tick = 1000, bool enable_lock = true);
  ~TimerWheel();

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

  void MoveOn();
  size_t GetTimerCount() const;
  tick_t GetCurrentTick() const;

 private:
  CCB_NOT_COPYABLE_AND_MOVABLE(TimerWheel);

  std::shared_ptr<TimerWheelImpl> pimpl_;
};

class TimerOwner {
 public:
  TimerOwner();
  ~TimerOwner();  // delete the timer

  bool has_timer() const {
    return static_cast<bool>(timer_);
  }
  void Cancel();

 private:
  CCB_NOT_COPYABLE_AND_MOVABLE(TimerOwner);

  std::unique_ptr<TimerWheelNode> timer_;
  std::shared_ptr<TimerWheelImpl> timer_wheel_;
  friend class TimerWheelImpl;
};

}  // namespace ccb

#endif  // CCBASE_TIMER_WHEEL_H_
