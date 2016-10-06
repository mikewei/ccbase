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
#ifndef _EVENTFD_H
#define _EVENTFD_H

#include <sys/eventfd.h>
#include <poll.h>
#include <error.h>
#include <system_error>
#include "common.h"

LIB_NAMESPACE_BEGIN

class EventFd
{
public:
  // useful API
  EventFd() : EventFd(0, EFD_NONBLOCK) {}
  bool Notify() {
    return Write(1);
  }
  bool Get() {
    uint64_t val;
    return Read(&val);
  }
  bool GetWait(int timeout = -1) {
    struct pollfd pfd;
    pfd.fd = fd_;
    pfd.events = POLLIN;
    while (!Get()) {
      if (poll(&pfd, 1, timeout) <= 0) {
        return false;
      }
    }
    return true;
  }
public:
  // syscall wrapper
  EventFd(unsigned int initval, int flags) {
    int res = eventfd(initval, flags);
    if (res < 0) {
      throw std::system_error(errno, std::system_category(), "eventfd create fail");
    }
    fd_ = res;
  }
  ~EventFd() {
    close(fd_);
  }
  bool Write(uint64_t val) {
    int res = write(fd_, &val, sizeof(uint64_t));
    if (res < 0 && errno != EAGAIN) {
      throw std::system_error(errno, std::system_category(), "eventfd write fail");
    }
    return (res == sizeof(uint64_t));
  }
  bool Read(uint64_t* val) {
    int res = read(fd_, val, sizeof(uint64_t));
    if (res < 0 && errno != EAGAIN) {
      throw std::system_error(errno, std::system_category(), "eventfd read fail");
    }
    return (res == sizeof(uint64_t));
  }
  int fd() {
    return fd_;
  }
private:
  int fd_;
};

LIB_NAMESPACE_END

#endif
