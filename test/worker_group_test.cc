#include <atomic>
#include <thread>
#include "gtestx/gtestx.h"
#include "ccbase/worker_group.h"
#include "ccbase/token_bucket.h"

#define QSIZE 1000000
#define DEFAULT_HZ 1000000
#define DEFAULT_TIME 1500

DECLARE_uint64(hz);

class WorkerGroupTest : public testing::Test
{
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
  usleep(1000);
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
    ccb::Worker::self()->PostTask([this] {
      val++;
    });
  });
  usleep(20000);
  ASSERT_EQ(2, val);
}

TEST_F(WorkerGroupTest, WorkerTls) {
  worker_group_1_.PostTask(0, [] {
      ccb::Worker::tls<int>()++;
  });
  worker_group_1_.PostTask(0, [] {
      ccb::Worker::tls<int>()++;
  });
  worker_group_1_.PostTask(0, [this] {
      val = ccb::Worker::tls<int>();
  });
  usleep(20000);
  ASSERT_EQ(2, val);
}

PERF_TEST_F_OPT(WorkerGroupTest, PostTaskPerf, DEFAULT_HZ, DEFAULT_TIME) {
  ASSERT_TRUE(worker_group_1_.PostTask([]{})) << PERF_ABORT;
}

PERF_TEST_F_OPT(WorkerGroupTest, PostSharedTaskPerf, DEFAULT_HZ, DEFAULT_TIME) {
  static ccb::ClosureFunc<void()> f{[]{}};
  ASSERT_TRUE(worker_group_1_.PostTask(f)) << PERF_ABORT;
}
