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
#include <sys/prctl.h>
#include <utility>
#include "ccbase/thread.h"

namespace ccb {

static void SetThreadName(const std::string& name) {
  if (name.size() > 0) {
    char buf[17];
    prctl(PR_GET_NAME, buf, 0, 0, 0);
    buf[16] = 0;
    std::string str(buf);
    size_t slash_pos = str.find_last_of('/');
    if (slash_pos != std::string::npos) {
      str.erase(slash_pos);
    }
    str.append("/");
    str.append(name);
    prctl(PR_SET_NAME, str.c_str(), 0, 0, 0);
  }
}

std::thread CreateThread(ClosureFunc<void()> func) {
  return std::thread(std::move(func));
}

std::thread CreateThread(const std::string& name, ClosureFunc<void()> func) {
  return std::thread([name, func] {
    SetThreadName(name);
    func();
  });
}

void CreateDetachedThread(ClosureFunc<void()> func) {
  std::thread(std::move(func)).detach();
}

void CreateDetachedThread(const std::string& name, ClosureFunc<void()> func) {
  std::thread([name, func] {
    SetThreadName(name);
    func();
  }).detach();
}

}  // namespace ccb
