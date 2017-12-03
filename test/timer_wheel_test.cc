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
#include <atomic>
#include <thread>
#include "gtestx/gtestx.h"
#include "ccbase/timer_wheel.h"

class TimerWheelTest : public testing::Test {
 protected:
  TimerWheelTest() {
  }
  void SetUp() {
  }
  void TearDown() {
  }
  ccb::TimerWheel tw_;
  int timers_ = 0;
  size_t count_ = 0;
};

TEST_F(TimerWheelTest, Simple) {
  int check = 0;
  // 0ms
  tw_.AddTimer(0, [&check] {
    check--;
  });
  check++;
  tw_.MoveOn();
  ASSERT_EQ(0, check);
  // 5ms
  tw_.AddTimer(5, [&check] {
    check -= 5;
  });
  check += 5;
  usleep(2000);
  tw_.MoveOn();
  EXPECT_EQ(5, check);
  usleep(4000);
  tw_.MoveOn();
  ASSERT_EQ(0, check);
  // 500ms
  tw_.AddTimer(500, [&check] {
    check -= 500;
  });
  check += 500;
  usleep(501000);
  tw_.MoveOn();
  ASSERT_EQ(0, check);
}

TEST_F(TimerWheelTest, Owner) {
  int check = 0;
  {
    ccb::TimerOwner to;
    tw_.AddTimer(1, [&check] {
      check++;
    }, &to);
    EXPECT_EQ(1UL, tw_.GetTimerCount());
  }
  EXPECT_EQ(0UL, tw_.GetTimerCount());
  tw_.MoveOn();
  usleep(2000);
  tw_.MoveOn();
  EXPECT_EQ(0, check);

  {
    ccb::TimerOwner to;
    tw_.AddTimer(1, [&check] {
      check++;
    }, &to);
    EXPECT_EQ(1UL, tw_.GetTimerCount());
    usleep(2000);
    tw_.MoveOn();
    EXPECT_EQ(0UL, tw_.GetTimerCount());
    EXPECT_EQ(1, check--);
  }
  EXPECT_EQ(0UL, tw_.GetTimerCount());
  EXPECT_EQ(0, check);

  {
    ccb::TimerOwner to;
    tw_.AddTimer(1, [&check] {
      check++;
    }, &to);
    EXPECT_EQ(1UL, tw_.GetTimerCount());
    tw_.AddTimer(1, [&check] {
      check++;
    }, &to);
    EXPECT_EQ(1UL, tw_.GetTimerCount());
    usleep(2000);
    tw_.MoveOn();
    EXPECT_EQ(0UL, tw_.GetTimerCount());
    EXPECT_EQ(1, check--);
    tw_.AddTimer(1, [&check] {
      check++;
    }, &to);
    EXPECT_EQ(1UL, tw_.GetTimerCount());
  }
  EXPECT_EQ(0UL, tw_.GetTimerCount());
  usleep(2000);
  tw_.MoveOn();
  EXPECT_EQ(0, check);
}

TEST_F(TimerWheelTest, Cancel) {
  int check = 0;
  ccb::TimerOwner to;
  tw_.AddTimer(1, [&check] {
    check++;
  }, &to);
  EXPECT_EQ(1UL, tw_.GetTimerCount());
  to.Cancel();
  EXPECT_EQ(0UL, tw_.GetTimerCount());
  usleep(2000);
  tw_.MoveOn();
  EXPECT_EQ(0, check);

  tw_.ResetTimer(to, 1);
  EXPECT_EQ(1UL, tw_.GetTimerCount());
  usleep(2000);
  tw_.MoveOn();
  EXPECT_EQ(1, check);
  EXPECT_EQ(0UL, tw_.GetTimerCount());
  to.Cancel();
}

TEST_F(TimerWheelTest, Reset) {
  int check = 0;
  ccb::TimerOwner to;
  tw_.AddTimer(0, [&check] {
    check--;
  }, &to);
  // reset pending timer
  tw_.ResetTimer(to, 5);
  tw_.MoveOn();
  ASSERT_EQ(0, check);
  check++;
  usleep(10000);
  tw_.MoveOn();
  ASSERT_EQ(0, check);
  usleep(10000);
  // reset launched timer
  tw_.ResetTimer(to, 0);
  check++;
  tw_.MoveOn();
  ASSERT_EQ(0, check);
  // reset timer to period timer
  tw_.ResetTimer(to, 5);
  tw_.ResetPeriodTimer(to, 10);
  check += 2;
  usleep(25000);
  tw_.MoveOn();
  ASSERT_EQ(0, check);
}

static inline int64_t ts_diff(const timespec& ts1, const timespec& ts2) {
  return ((ts1.tv_sec - ts2.tv_sec) * 1000000000 + ts1.tv_nsec - ts2.tv_nsec);
}

TEST_F(TimerWheelTest, Period) {
  int check = 0;
  ASSERT_FALSE(tw_.AddPeriodTimer(0, []{}));
  ASSERT_TRUE(tw_.AddPeriodTimer(10, [&check] {
    check++;
  }));
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  for (int i = 1; i < 10; i++) {
    while (true) {
      usleep(500);
      tw_.MoveOn();
      struct timespec cts;
      clock_gettime(CLOCK_MONOTONIC, &cts);
      if (ts_diff(cts, ts) >= i*10*1000000)
        break;
    }
    EXPECT_EQ(i, check);
  }
}

TEST_F(TimerWheelTest, GetCurrentTick) {
  ccb::tick_t init_tick = tw_.GetCurrentTick();
  ccb::tick_t last_tick = init_tick;
  for (int i = 1; i < 10; i++) {
    usleep(500);
    tw_.MoveOn();
    ccb::tick_t cur_tick = tw_.GetCurrentTick();
    ASSERT_TRUE(last_tick == cur_tick || last_tick + 1 == cur_tick);
    last_tick = cur_tick;
  }
  ASSERT_LT(init_tick, last_tick);
}

TEST_F(TimerWheelTest, DeleteNodeTrackingTest) {
  int check = 0;
  ccb::TimerOwner* to = new ccb::TimerOwner;
  tw_.AddTimer(0, [&check, to] {
    check++;
    delete to;
  });
  tw_.AddTimer(0, [&check] {
    check++;
  }, to);
  EXPECT_EQ(2UL, tw_.GetTimerCount());
  tw_.MoveOn();
  ASSERT_EQ(1, check);
}

PERF_TEST_F(TimerWheelTest, AddTimerPerf) {
  timers_++;
  tw_.AddTimer(1, [this]{
    timers_--;
  });
  if ((count_ & 0x3ff) == 0) {
    tw_.MoveOn();
  }
  if ((++count_ & 0x3fffff) == 1) {
    fprintf(stderr, "pending %d timers\n", timers_);
  }
}

PERF_TEST_F(TimerWheelTest, ResetTimerPerf) {
  static ccb::TimerOwner owner;
  if (!owner.has_timer()) {
    tw_.AddTimer(1, []{}, &owner);
  }
  tw_.ResetTimer(owner, 1);
}

class TimerWheelNoLockTest : public testing::Test {
 protected:
  TimerWheelNoLockTest() {
  }
  void SetUp() {
  }
  void TearDown() {
  }
  ccb::TimerWheel tw_{1000, false};
  int timers_ = 0;
  size_t count_ = 0;
};

PERF_TEST_F(TimerWheelNoLockTest, AddTimerPerf) {
  timers_++;
  tw_.AddTimer(1, [this]{
    timers_--;
  });
  if ((count_ & 0x3ff) == 0) {
    tw_.MoveOn();
  }
  if ((++count_ & 0x3fffff) == 1) {
    fprintf(stderr, "pending %d timers\n", timers_);
  }
}

PERF_TEST_F(TimerWheelNoLockTest, ResetTimerPerf) {
  static ccb::TimerOwner owner;
  if (!owner.has_timer()) {
    tw_.AddTimer(1, []{}, &owner);
  }
  tw_.ResetTimer(owner, 1);
}

class TimerWheelMTTest : public testing::Test {
 protected:
  void SetUp() {
    thread_ = std::thread([this] {
      while (!stop_) {
        tw_.MoveOn();
        usleep(200);
      }
    });
  }
  void TearDown() {
    stop_ = true;
    thread_.join();
  }
  ccb::TimerWheel tw_;
  std::thread thread_;
  size_t count_ = 0;
  std::atomic<int> timers_ = {0};
  std::atomic<bool> stop_ = {false};
};

TEST_F(TimerWheelMTTest, Simple) {
  std::atomic<int> check{0};
  // 0ms
  tw_.AddTimer(0, [&check] {
    check--;
  });
  check++;
  usleep(1000);
  ASSERT_EQ(0, check);
  // 5ms
  tw_.AddTimer(5, [&check] {
    check -= 5;
  });
  check += 5;
  usleep(2000);
  EXPECT_EQ(5, check);
  usleep(4000);
  ASSERT_EQ(0, check);
  // 500ms
  tw_.AddTimer(500, [&check] {
    check -= 500;
  });
  check += 500;
  usleep(501000);
  ASSERT_EQ(0, check);
}

PERF_TEST_F(TimerWheelMTTest, AddTimerPerf) {
  timers_++;
  bool res = tw_.AddTimer(1, [this]{
    timers_--;
  });
  ASSERT_TRUE(res) << PERF_ABORT;
  if ((++count_ & 0x3fffff) == 1) {
    fprintf(stderr, "pending %d timers\n", static_cast<int>(timers_));
  }
}

