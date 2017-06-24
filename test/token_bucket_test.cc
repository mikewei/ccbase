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
#include "gtestx/gtestx.h"
#include "ccbase/token_bucket.h"

class TokenBucket : public testing::Test {
 protected:
  TokenBucket() : tb_(10000, 10000) {}
  virtual ~TokenBucket() {}
  virtual void SetUp() {
  }
  virtual void TearDown() {
  }
  ccb::TokenBucket tb_;
};

TEST_F(TokenBucket, Get) {
  const unsigned int n = 10000;
  ASSERT_GE(tb_.tokens(), n);
  ASSERT_TRUE(tb_.Check(n));
  ASSERT_TRUE(tb_.Get(tb_.tokens()));
  ASSERT_EQ(0U, tb_.tokens());
  ASSERT_FALSE(tb_.Check(1));
  ASSERT_FALSE(tb_.Get(1));
  sleep(1);
  tb_.Gen();
  ASSERT_EQ(n, tb_.tokens());
  for (unsigned int i = 0; i < n; i++) {
    ASSERT_TRUE(tb_.Check(1));
    ASSERT_TRUE(tb_.Get(1));
  }
  ASSERT_FALSE(tb_.Check(1));
  ASSERT_FALSE(tb_.Get(1));
}

PERF_TEST_F(TokenBucket, Get_Perf) {
  if (!tb_.Get(1)) tb_.Mod(10000, 10000);
}

