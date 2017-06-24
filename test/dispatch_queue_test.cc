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
#include "ccbase/dispatch_queue.h"
#include "ccbase/token_bucket.h"

#define QSIZE 1000000
#define DEFAULT_HZ 1000000
#define DEFAULT_TIME 1500

DECLARE_uint64(hz);

class DispatchQueueTest : public testing::Test {
 protected:
  DispatchQueueTest()
      : dispatch_queue_(QSIZE) {}
  void SetUp() {}
  void TearDown() {}

  ccb::DispatchQueue<int> dispatch_queue_;
};

TEST_F(DispatchQueueTest, UnregisterProducer) {
  auto producer = dispatch_queue_.RegisterProducer();
  auto consumer = dispatch_queue_.RegisterConsumer();
  ASSERT_NE(nullptr, producer);
  ASSERT_NE(nullptr, consumer);
  producer->Push(1);
  int val = 0;
  consumer->Pop(&val);
  ASSERT_EQ(1, val);
  producer->Unregister();
  producer = dispatch_queue_.RegisterProducer();
  producer->Push(2);
  consumer->Pop(&val);
  ASSERT_EQ(2, val);
}

PERF_TEST_F(DispatchQueueTest, OneshotProducer) {
  static auto producer = dispatch_queue_.RegisterProducer();
  static auto consumer = dispatch_queue_.RegisterConsumer();
  static int count = 0;
  producer->Unregister();
  producer = dispatch_queue_.RegisterProducer();
  producer->Push(++count);
  int val = 0;
  consumer->Pop(&val);
  ASSERT_EQ(count, val) << PERF_ABORT;
}

class DispatchQueuePerfTest : public testing::Test {
 protected:
  DispatchQueuePerfTest()
      : dispatch_queue_(QSIZE),
        r1_count_(0),
        r2_count_(0),
        overflow_(0),
        stop_(false),
        err_found_(false) {}
  void SetUp() {
    r1_thread_ = std::thread(&DispatchQueuePerfTest::ReadThread, this, 1);
    r2_thread_ = std::thread(&DispatchQueuePerfTest::ReadThread, this, 2);
    timer_thread_ = std::thread([this] {
      unsigned count = 0;
      while (!stop_.load(std::memory_order_relaxed)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        if (++count % 100 == 0) OnTimer();
      }
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    w2_thread_ = std::thread(&DispatchQueuePerfTest::WriteThread, this, 2);
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
    std::cout << "r1_read " << r1_count_ << "/s  r2_read " << r2_count_
              << "  overflow " <<  overflow_ << std::endl;
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

PERF_TEST_F_OPT(DispatchQueuePerfTest, IO_Perf, DEFAULT_HZ, DEFAULT_TIME) {
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


