#include <atomic>
#include <thread>
#include "gtestx/gtestx.h"
#include "ccbase/dispatch_queue.h"
#include "ccbase/token_bucket.h"

#define QSIZE 1000000
#define DEFAULT_HZ 1000000
#define DEFAULT_TIME 1500

DECLARE_uint64(hz);

class DispatchQueueTest : public testing::Test
{
protected:
  DispatchQueueTest() :
    dispatch_queue_(QSIZE),
    r1_count_(0),
    r2_count_(0),
    overflow_(0),
       stop_(false),
    err_found_(false) {}
  void SetUp() {
    r1_thread_ = std::thread(&DispatchQueueTest::ReadThread, this, 1);
    r2_thread_ = std::thread(&DispatchQueueTest::ReadThread, this, 2);
    timer_thread_ = std::thread([this] {
      unsigned count = 0;
      while (!stop_.load(std::memory_order_relaxed)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        if (++count % 100 == 0) OnTimer();
      }
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    w2_thread_ = std::thread(&DispatchQueueTest::WriteThread, this, 2);
    std::cout << "producer thread #" << 1 << " start" << std::endl;
  }
  void TearDown() {
    stop_.store(true, std::memory_order_relaxed);
    r1_thread_.join();
    r2_thread_.join();
    w2_thread_.join();
    timer_thread_.join();
  }
  void ReadThread(int id) {
    std::cout << "consumer thread #" << id << " start" << std::endl;
    auto q = dispatch_queue_.RegisterConsumer();
    auto& count = (id == 1 ? r1_count_ : r2_count_);
    int val, check[2] = {0, 0};
    while (!stop_.load(std::memory_order_relaxed)) {
      if (!q->Pop(&val)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        continue;
      }
      if (val/4 != check[val%2]++) {
        std::cout << "ReadThread #" << id << " check " << val%2 << " failed:" 
              << "val/4=" << val/4 << " check=" << check[val%2]-1 << std::endl;
        err_found_.store(true, std::memory_order_relaxed);
        break;
      }
      count.fetch_add(1, std::memory_order_relaxed);
    }
    std::cout << "consumer thread #" << id << " exit" << std::endl;
  }
  void WriteThread(int id) {
    std::cout << "producer thread #" << id << " start" << std::endl;
    uint64_t hz = FLAGS_hz ? FLAGS_hz : DEFAULT_HZ;
    std::cerr << "hz=" << hz << std::endl;
    ccb::TokenBucket tb(hz, hz/5, hz/500);
    auto q = dispatch_queue_.RegisterProducer();
    int val = 1;
    while (!stop_.load(std::memory_order_relaxed)) {
      if (tb.Get(1)) {
        if (q->Push(val/2%2, val)) {
          val += 2;
        }
      } else {
        usleep(1000);
        tb.Gen();
      }
    }
    std::cout << "producer thread #" << id << " exit" << std::endl;
  }
  void OnTimer() {
    std::cout << "r1_read " << r1_count_ << "/s  r2_read " << r2_count_ << "  overflow " <<  overflow_ << std::endl;
    r1_count_ = r2_count_ = overflow_ = 0;
  }
  ccb::DispatchQueue<int> dispatch_queue_;
  std::atomic<uint64_t> r1_count_;
  std::atomic<uint64_t> r2_count_;
  std::atomic<uint64_t> overflow_;
  std::thread r1_thread_;
  std::thread r2_thread_;
  std::thread w2_thread_;
  std::thread timer_thread_;
  std::atomic_bool stop_;
  std::atomic_bool err_found_;
};

PERF_TEST_F_OPT(DispatchQueueTest, IO_Perf, DEFAULT_HZ, DEFAULT_TIME) {
  static int val = 0;
  static auto q = dispatch_queue_.RegisterProducer();
  if (q->Push(val/2%2, val)) {
    val += 2;
  } else {
    overflow_++;
  }
  if ((val & 0xfff) == 0) {
    ASSERT_FALSE(err_found_.load(std::memory_order_relaxed)) << PERF_ABORT;
  }
}
