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
#include <unistd.h>
#include <memory>
#include <thread>
#include "gtestx/gtestx.h"
#include "ccbase/token_bucket.h"

class TokenBucketTest : public testing::TestWithParam<bool> {
 protected:
  TokenBucketTest() {}
  virtual ~TokenBucketTest() {}
  virtual void SetUp() {
    tb_.reset(new ccb::TokenBucket(n_, n_, n_, nullptr, GetParam()));
  }
  virtual void TearDown() {
  }
  std::unique_ptr<ccb::TokenBucket> tb_;
  static constexpr unsigned int n_ = 10000;
};

constexpr unsigned int TokenBucketTest::n_;

INSTANTIATE_TEST_CASE_P(IsEnableLock, TokenBucketTest, testing::Values(false, true));

TEST_P(TokenBucketTest, Get) {
  ASSERT_GE(tb_->tokens(), n_);
  ASSERT_TRUE(tb_->Check(n_));
  ASSERT_TRUE(tb_->Get(tb_->tokens()));
  ASSERT_EQ(0U, tb_->tokens());
  ASSERT_FALSE(tb_->Check(1));
  ASSERT_FALSE(tb_->Get(1));
  sleep(1);
  tb_->Gen();
  ASSERT_EQ(n_, tb_->tokens());
  for (unsigned int i = 0; i < n_; i++) {
    ASSERT_TRUE(tb_->Check(1));
    ASSERT_TRUE(tb_->Get(1));
  }
  ASSERT_FALSE(tb_->Check(1));
  ASSERT_FALSE(tb_->Get(1));
}

PERF_TEST_P(TokenBucketTest, GetPerf) {
  if (!tb_->Get(1)) tb_->Mod(n_, n_, n_);
}

PERF_TEST_P(TokenBucketTest, GenPerf) {
  tb_->Gen();
}

class TokenBucketMTTest : public testing::Test {
 protected:
  TokenBucketMTTest() : counter_(0), stop_flag_(false) {}
  virtual ~TokenBucketMTTest() {}
  virtual void SetUp() {
    tb_.reset(new ccb::TokenBucket(n_, n_, 0, nullptr, true));
    gen_thread_1_ = std::thread([this] {
      while (!stop_flag_) tb_->Gen();
    });
    gen_thread_2_ = std::thread([this] {
      while (!stop_flag_) tb_->Gen();
    });
    get_thread_1_ = std::thread([this] {
      while (!stop_flag_) {
        if (tb_->Get()) counter_++;
      }
    });
    mon_thread_ = std::thread([this] {
      size_t last_counter = 0;
      while (!stop_flag_) {
        sleep(1);
        size_t cur_counter = counter_.load();
        fprintf(stderr, "get %lu tokens/s\n", cur_counter - last_counter);
        last_counter = cur_counter;
      }
    });
  }
  virtual void TearDown() {
    stop_flag_ = true;
    mon_thread_.join();
    get_thread_1_.join();
    gen_thread_2_.join();
    gen_thread_1_.join();
  }
  static constexpr unsigned int n_ = 10000;
  std::thread gen_thread_1_;
  std::thread gen_thread_2_;
  std::thread get_thread_1_;
  std::thread mon_thread_;
  std::atomic<size_t> counter_;
  std::atomic<bool> stop_flag_;
  std::unique_ptr<ccb::TokenBucket> tb_;
};

constexpr unsigned int TokenBucketMTTest::n_;

PERF_TEST_F(TokenBucketMTTest, Get) {
  while (!tb_->Get()) {
  }
  counter_++;
}

