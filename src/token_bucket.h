/* Copyright (c) 2012-2015, Bin Wei <bin@vip.qq.com>
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
 *     * The name of of its contributors may not be used to endorse or 
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
#ifndef _TOKEN_BUCKET_H
#define _TOKEN_BUCKET_H

#include <sys/time.h>
#include <stdint.h>
#include "ccbase/common.h"

namespace ccb {

class TokenBucket
{
public:
  TokenBucket(uint32_t tokens_per_sec);
  TokenBucket(uint32_t tokens_per_sec, uint32_t bucket_size);
  TokenBucket(uint32_t tokens_per_sec, uint32_t bucket_size,
              uint32_t init_tokens, const struct timeval* tv_now = nullptr);
  // copyable
  TokenBucket(const TokenBucket&) = default;
  TokenBucket& operator=(const TokenBucket&) = default;

  void Gen(const struct timeval* tv_now = nullptr);
  bool Get(uint32_t need_tokens = 1);
  void Mod(uint32_t tokens_per_sec, uint32_t bucket_size);
  uint32_t tokens() const;
  bool Check(uint32_t need_tokens);
  int Overdraft(uint32_t need_tokens);
private:
  // not movable
  TokenBucket(TokenBucket&&) = delete;
  TokenBucket& operator=(TokenBucket&&) = delete;

  uint32_t tokens_per_sec_;
  uint32_t bucket_size_;
  int64_t token_count_;
  uint64_t last_gen_time_;
  uint64_t last_calc_delta_;
};

inline uint32_t TokenBucket::tokens() const
{
  return (uint32_t)(token_count_ <= 0 ? 0 : token_count_);
}

} // namespace ccb

#endif
