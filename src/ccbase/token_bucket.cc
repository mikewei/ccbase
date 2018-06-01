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

namespace {

// conditional locker
class Locker {
 public:
  Locker(std::mutex* m, bool on)
      : m_(*m), on_(on) {
    if (on_) m_.lock();
  }
  ~Locker() {
    if (on_) m_.unlock();
  }

 private:
  std::mutex& m_;
  bool on_;
};

}  // namespace

TokenBucket::TokenBucket(uint32_t tokens_per_sec)
    : TokenBucket(tokens_per_sec,
                  tokens_per_sec / 5,
                  true) {
}

TokenBucket::TokenBucket(uint32_t tokens_per_sec,
                         uint32_t bucket_size)
    : TokenBucket(tokens_per_sec,
                  bucket_size,
                  bucket_size,
                  nullptr,
                  true) {
}

TokenBucket::TokenBucket(uint32_t tokens_per_sec,
                         uint32_t bucket_size,
                         uint32_t init_tokens,
                         const struct timeval* tv_now,
                         bool enable_lock_for_mt)
    : tokens_per_sec_(tokens_per_sec),
      bucket_size_(bucket_size ? bucket_size : 1),
      token_count_(init_tokens),
      enable_lock_(enable_lock_for_mt) {
  struct timeval tv;
  if (!tv_now) {
    tv_now = &tv;
    gettimeofday(&tv, nullptr);
  }
  last_gen_time_ = TV2US(tv_now);
  last_calc_delta_ = 0;
}

void TokenBucket::Mod(uint32_t tokens_per_sec, uint32_t bucket_size) {
  Locker locker(&gen_mutex_, enable_lock_);
  tokens_per_sec_ = tokens_per_sec;
  bucket_size_ = bucket_size;
}

void TokenBucket::Mod(uint32_t tokens_per_sec,
                      uint32_t bucket_size,
                      uint32_t init_tokens) {
  Locker locker(&gen_mutex_, enable_lock_);
  tokens_per_sec_ = tokens_per_sec;
  bucket_size_ = bucket_size;
  token_count_.store(init_tokens, std::memory_order_relaxed);
}

void TokenBucket::Gen(const struct timeval* tv_now) {
  struct timeval tv;
  uint64_t us_now, us_past;
  uint64_t new_tokens, calc_delta;
  int64_t new_token_count, cur_token_count;

  Locker locker(&gen_mutex_, enable_lock_);

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
  cur_token_count = token_count_.load(std::memory_order_relaxed);
  new_token_count = cur_token_count + new_tokens;
  if (new_token_count < cur_token_count ||
      new_token_count > static_cast<int64_t>(bucket_size_)) {
    new_token_count = bucket_size_;
  }
  if (!enable_lock_) {
    token_count_.store(new_token_count, std::memory_order_relaxed);
  } else {
    token_count_.fetch_add(new_token_count - cur_token_count);
  }
}

bool TokenBucket::Check(uint32_t need_tokens) {
  int64_t token_count = token_count_.load(std::memory_order_relaxed);
  if (token_count < static_cast<int64_t>(need_tokens)) {
    return false;
  }
  return true;
}

bool TokenBucket::Get(uint32_t need_tokens) {
  int64_t token_count = token_count_.load(std::memory_order_relaxed);
  if (token_count < static_cast<int64_t>(need_tokens)) {
    return false;
  }
  if (!enable_lock_) {
    token_count_.store(token_count - need_tokens, std::memory_order_relaxed);
  } else {
    int64_t cur_tokens = token_count_.fetch_sub(need_tokens);
    if (cur_tokens < -static_cast<int64_t>(bucket_size_)) {
      // rollback if overdraft too much
      token_count_.fetch_add(need_tokens);
      return false;
    }
  }
  return true;
}

int TokenBucket::Overdraft(uint32_t need_tokens) {
  int64_t cur_tokens;
  if (!enable_lock_) {
    cur_tokens = token_count_.load(std::memory_order_relaxed);
    token_count_.store(cur_tokens - need_tokens, std::memory_order_relaxed);
  } else {
    cur_tokens = token_count_.fetch_sub(need_tokens);
  }
  return (cur_tokens < static_cast<int64_t>(need_tokens) ?
              need_tokens - cur_tokens : 0);
}

}  // namespace ccb

