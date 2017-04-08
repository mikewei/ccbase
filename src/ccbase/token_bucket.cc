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
#include <sys/types.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>
#include <ctype.h>
#include <time.h>
#include <stdlib.h>
#include <errno.h>

#include "ccbase/token_bucket.h"

#define TV2US(ptv) ((ptv)->tv_sec * 1000000 + (ptv)->tv_usec)

namespace ccb {

TokenBucket::TokenBucket(uint32_t tokens_per_sec)
  : TokenBucket(tokens_per_sec, tokens_per_sec / 5) {}

TokenBucket::TokenBucket(uint32_t tokens_per_sec, uint32_t bucket_size)
  : TokenBucket(tokens_per_sec, bucket_size, bucket_size) {}

TokenBucket::TokenBucket(uint32_t tokens_per_sec, uint32_t bucket_size,
                         uint32_t init_tokens, const struct timeval* tv_now) {
  struct timeval tv;

  tokens_per_sec_ = tokens_per_sec;
  bucket_size_ = bucket_size ? bucket_size : 1;
  token_count_ = init_tokens;
  if (!tv_now) {
    tv_now = &tv;
    gettimeofday(&tv, nullptr);
  }
  last_gen_time_ = TV2US(tv_now);
  last_calc_delta_ = 0;
}

void TokenBucket::Mod(uint32_t tokens_per_sec, uint32_t bucket_size) {
  tokens_per_sec_ = tokens_per_sec;
  bucket_size_ = bucket_size;
}

void TokenBucket::Gen(const struct timeval* tv_now) {
  struct timeval tv;
  uint64_t us_now, us_past;
  uint64_t new_tokens, calc_delta;
  int64_t new_token_count;

  if (tv_now == nullptr) {
    tv_now = &tv;
    gettimeofday(&tv, nullptr);
  }
  us_now = TV2US(tv_now);
  if (us_now < last_gen_time_) {
    last_gen_time_ = us_now;
    return;
  }

  us_past = us_now - last_gen_time_;
  new_tokens = (((uint64_t)tokens_per_sec_ * us_past + last_calc_delta_)
                 / 1000000);
  calc_delta = (((uint64_t)tokens_per_sec_ * us_past + last_calc_delta_)
                 % 1000000);

  last_gen_time_ = us_now;
  last_calc_delta_ = calc_delta;
  new_token_count = token_count_ + new_tokens;
  if (new_token_count < token_count_) {
    token_count_ = bucket_size_;
    return;
  }
  if (new_token_count > bucket_size_) {
    new_token_count = bucket_size_;
  }
  token_count_ = new_token_count;

  return;
}

bool TokenBucket::Check(uint32_t need_tokens) {
  if (token_count_ < (int64_t)need_tokens) {
    return false;
  }
  return true;
}

bool TokenBucket::Get(uint32_t need_tokens) {
  if (token_count_ < (int64_t)need_tokens) {
    return false;
  }
  token_count_ -= need_tokens;
  return true;
}

int TokenBucket::Overdraft(uint32_t need_tokens) {
  token_count_ -= need_tokens;
  return (token_count_ < 0 ? -token_count_ : 0);
}

}  // namespace ccb

