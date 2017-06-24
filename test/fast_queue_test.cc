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
#include <type_traits>
#include "gtestx/gtestx.h"
#include "ccbase/fast_queue.h"

#define QSIZE (1000000)

using TestTypes = testing::Types<std::true_type, std::false_type>;

template <class BoolType>
class FastQueueTest : public testing::Test {
 protected:
  FastQueueTest() :
    fq_(QSIZE),
    count_(0),
    overflow_(0),
       stop_(false),
       err_found_(false) {}
  void SetUp() {
    thread_ = std::thread(&FastQueueTest::ThreadMain, this);
    timer_thread_ = std::thread([this] {
      unsigned count = 0;
      while (!stop_.load(std::memory_order_relaxed)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        if (++count % 100 == 0) OnTimer();
      }
    });
  }
  void TearDown() {
    stop_.store(true, std::memory_order_relaxed);
    thread_.join();
    timer_thread_.join();
  }
  void ThreadMain() {
    std::cout << "consumer thread start" << std::endl;
    int val, check = 0;
    while (!stop_.load(std::memory_order_relaxed)) {
      if (!fq_.PopWait(&val, 100)) {
        if (!stop_) {
          std::cout << "consumer read nothing!" << std::endl;
          exit(1);
        }
        continue;
      }
      if (val != check++) {
        std::cout << "thread_main check failed" << std::endl;
        err_found_.store(true, std::memory_order_relaxed);
        break;
      }
      count_.fetch_add(1, std::memory_order_relaxed);
    }
    std::cout << "consumer thread exit" << std::endl;
  }
  void OnTimer() {
    std::cout << "read " << count_ << "/s  overflow " <<  overflow_ << std::endl;
    count_ = overflow_ = 0;
  }
  ccb::FastQueue<int, BoolType::value> fq_;
  std::atomic<uint64_t> count_;
  std::atomic<uint64_t> overflow_;
  std::thread thread_;
  std::thread timer_thread_;
  std::atomic_bool stop_;
  std::atomic_bool err_found_;
};
TYPED_TEST_CASE(FastQueueTest, TestTypes);

TYPED_PERF_TEST_OPT(FastQueueTest, IO_Perf, 1000000, 1500) {
  static int val = 0;
  if (this->fq_.Push(val)) {
    val++;
  } else {
    this->overflow_++;
  }
  if ((val & 0xfff) == 0) {
    EXPECT_FALSE(this->err_found_.load(std::memory_order_relaxed));
    EXPECT_EQ(0UL, this->overflow_);
  }
}

