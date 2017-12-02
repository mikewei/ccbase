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
#include "ccbase/worker_pool.h"

#define QSIZE 100000
#define NOP_TASK_HZ 1000000
#define MS1_TASK_HZ 100000
#define DEFAULT_TIME 1500

DECLARE_uint64(hz);

class WorkerPoolTest : public testing::Test {
 protected:
  void SetUp() {
  }
  void TearDown() {
  }

  ccb::WorkerPool worker_pool_1_{1, 8, QSIZE};
  ccb::WorkerPool worker_pool_2_{2, 256, QSIZE};
  std::atomic_int val{0};
};

TEST_F(WorkerPoolTest, PostTask) {
  worker_pool_1_.PostTask([this] {
    val++;
  });
  worker_pool_2_.PostTask([this] {
    val++;
  });
  usleep(10000);
  ASSERT_EQ(2, val);
}

TEST_F(WorkerPoolTest, PostDelayTask) {
  worker_pool_1_.PostTask([this] {
    val++;
  }, 1);
  worker_pool_2_.PostTask([this] {
    val++;
  }, 1);
  usleep(10000);
  ASSERT_EQ(2, val);
}

TEST_F(WorkerPoolTest, PostPeriodTask) {
  worker_pool_1_.PostPeriodTask([this] {
    if (val < 10) val++;
  }, 1);
  usleep(50000);
  ASSERT_EQ(10, val);
}

TEST_F(WorkerPoolTest, WorkerSelf) {
  using Worker = ccb::WorkerPool::Worker;
  worker_pool_1_.PostTask([this] {
    val++;
    Worker::self()->worker_pool()->PostTask([this] {
      val++;
    });
  });
  usleep(20000);
  ASSERT_EQ(2, val);
}

namespace {
  struct TestContext : public ccb::WorkerPool::Context {
    int Get() const {
      return 1;
    }
  };
}  // namespace

TEST_F(WorkerPoolTest, UseContext) {
  ccb::WorkerPool worker_pool{1, 1, 10, [](size_t) {
    return std::make_shared<TestContext>();
  }};
  int value = 0;
  worker_pool.PostTask([&value] {
    value = ccb::WorkerPool::Worker::self()->context<TestContext>()->Get();
  });
  usleep(10000);
  ASSERT_EQ(1, value);
}

PERF_TEST_F_OPT(WorkerPoolTest, PostNopTaskPerf, NOP_TASK_HZ, DEFAULT_TIME) {
  static size_t counter = 0;
  if (++counter == NOP_TASK_HZ) {
    fprintf(stderr, "concurrent workers: %lu/%lu\n",
                                         worker_pool_1_.concurrent_workers(),
                                         worker_pool_1_.size());
    counter = 0;
  }
  ASSERT_TRUE(worker_pool_1_.PostTask([]{})) << PERF_ABORT;
}

PERF_TEST_F_OPT(WorkerPoolTest, PostSleepTaskPerf, MS1_TASK_HZ, DEFAULT_TIME) {
  static size_t counter = 0;
  if (++counter == MS1_TASK_HZ) {
    fprintf(stderr, "concurrent workers: %lu/%lu\n",
                                         worker_pool_2_.concurrent_workers(),
                                         worker_pool_2_.size());
    counter = 0;
  }
  ASSERT_TRUE(worker_pool_2_.PostTask([]{usleep(1000);})) << PERF_ABORT;
}

PERF_TEST_F_OPT(WorkerPoolTest, PostSleepChangePerf, MS1_TASK_HZ, DEFAULT_TIME) {
  static size_t counter = 0;
  static size_t round = 0;
  if (++counter == MS1_TASK_HZ) {
    fprintf(stderr, "concurrent workers: %lu/%lu  round: %lu\n",
                                         worker_pool_2_.concurrent_workers(),
                                         worker_pool_2_.size(),
                                         round);
    counter = 0;
    round++;
  }
  if (round/20%2 == 1) {
    if (counter%10 != 0) {  // 1/10 workload
      return;
    }
  }
  ASSERT_TRUE(worker_pool_2_.PostTask([]{usleep(1000);})) << PERF_ABORT;
}

PERF_TEST_F_OPT(WorkerPoolTest, PostSharedTaskPerf, NOP_TASK_HZ, DEFAULT_TIME) {
  static ccb::ClosureFunc<void()> f{[]{}};
  ASSERT_TRUE(worker_pool_1_.PostTask(f)) << PERF_ABORT;
}

PERF_TEST_F(WorkerPoolTest, SpawnThreadPostTask) {
  std::thread([this] {
    ASSERT_TRUE(worker_pool_1_.PostTask([]{})) << PERF_ABORT;
  }).join();
}

TEST(WorkerPoolDtorTest, DestructBeforeThreadExit) {
  ccb::WorkerPool worker_pool{2, 8, QSIZE};
  worker_pool.PostTask([]{});
}
