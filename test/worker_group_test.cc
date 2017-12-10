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
#include <atomic>
#include <thread>
#include "gtestx/gtestx.h"
#include "ccbase/worker_group.h"
#include "ccbase/token_bucket.h"

#define QSIZE 1000000
#define DEFAULT_HZ 1000000
#define DEFAULT_TIME 1500

DECLARE_uint64(hz);

using Worker = ccb::WorkerGroup::Worker;

class WorkerGroupTest : public testing::Test {
 protected:
  void SetUp() {
  }
  void TearDown() {
  }

  ccb::WorkerGroup worker_group_1_{2, QSIZE};
  ccb::WorkerGroup worker_group_2_{2, QSIZE};
  std::atomic_int val{0};
};

TEST_F(WorkerGroupTest, PostTask) {
  worker_group_1_.PostTask([this] {
    val++;
  });
  worker_group_1_.PostTask(1, [this] {
    val++;
  });
  worker_group_2_.PostTask([this] {
    val++;
  });
  worker_group_2_.PostTask(0, [this] {
    val++;
  });
  usleep(10000);
  ASSERT_EQ(4, val);
  worker_group_1_.PostTask([this] {
    val--;
  }, 50);
  worker_group_1_.PostTask(1, [this] {
    val--;
  }, 10);
  worker_group_2_.PostTask([this] {
    val--;
  }, 20);
  worker_group_2_.PostTask(0, [this] {
    val--;
  }, 30);
  ASSERT_EQ(4, val);
  usleep(55000);
  ASSERT_EQ(0, val);
}

TEST_F(WorkerGroupTest, PostPeriodTask) {
  worker_group_1_.PostPeriodTask([this] {
    val++;
  }, 20);
  worker_group_1_.PostPeriodTask(1, [this] {
    val++;
  }, 20);
  worker_group_2_.PostPeriodTask([this] {
    val++;
  }, 20);
  worker_group_2_.PostPeriodTask(0, [this] {
    val++;
  }, 20);
  usleep(5000);
  ASSERT_EQ(0, val);
  usleep(20000);
  ASSERT_EQ(4, val);
  usleep(20000);
  ASSERT_EQ(8, val);
  usleep(20000);
  ASSERT_EQ(12, val);
}

TEST_F(WorkerGroupTest, WorkerSelf) {
  worker_group_1_.PostTask([this] {
    val++;
    Worker::self()->PostTask([this] {
      val++;
    });
  });
  usleep(20000);
  ASSERT_EQ(2, val);
}

TEST_F(WorkerGroupTest, WorkerTls) {
  worker_group_1_.PostTask(0, [] {
    Worker::tls<int>()++;
  });
  worker_group_1_.PostTask(0, [] {
    Worker::tls<int>()++;
  });
  worker_group_1_.PostTask(0, [this] {
    val = Worker::tls<int>();
  });
  usleep(20000);
  ASSERT_EQ(2, val);
}

TEST_F(WorkerGroupTest, WorkerPoller) {
  ccb::WorkerGroup::Poller* poller1 = nullptr;
  ccb::WorkerGroup::Poller* poller2 = nullptr;
  worker_group_1_.PostTask(0, [&poller1] {
    poller1 = Worker::self()->poller();
    poller1->Poll(0);
  });
  worker_group_1_.PostTask(1, [&poller2] {
    poller2 = Worker::self()->poller();
    poller2->Poll(0);
  });
  usleep(20000);
  ASSERT_NE(nullptr, poller1);
  ASSERT_EQ(poller1, poller2);
}

PERF_TEST_F_OPT(WorkerGroupTest, PostTaskPerf, DEFAULT_HZ, DEFAULT_TIME) {
  ASSERT_TRUE(worker_group_1_.PostTask([]{})) << PERF_ABORT;
}

PERF_TEST_F_OPT(WorkerGroupTest, PostSharedTaskPerf, DEFAULT_HZ, DEFAULT_TIME) {
  static ccb::ClosureFunc<void()> f{[]{}};
  ASSERT_TRUE(worker_group_1_.PostTask(f)) << PERF_ABORT;
}

PERF_TEST_F(WorkerGroupTest, SpawnThreadPostTask) {
  std::thread([this] {
    ASSERT_TRUE(worker_group_1_.PostTask([]{})) << PERF_ABORT;
  }).join();
}

TEST(WorkerGroupDtorTest, DestructBeforeThreadExit) {
  ccb::WorkerGroup worker_group{1, QSIZE};
  worker_group.PostTask([]{});
}
