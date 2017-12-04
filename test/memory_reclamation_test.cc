/* Copyright (c) 2016-2017, Bin Wei <bin@vip.qq.com>
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
#include <atomic>
#include <thread>
#include <type_traits>
#include "gtestx/gtestx.h"
#include "ccbase/memory_reclamation.h"

class TraceableObj {
 public:
  TraceableObj() : val_(1) {}
  ~TraceableObj() { val_ = 0; }

  int val() const {
    return val_;
  }
  static size_t allocated_objs() {
    return allocated_objs_;
  }

  static void* operator new(size_t sz) {
    allocated_objs_++;
    return ::operator new(sz);
  }
  static void operator delete(void* ptr, size_t sz) {
    allocated_objs_--;
    ::operator delete(ptr);
  }

 private:
  int val_;
  static std::atomic<size_t> allocated_objs_;
};

std::atomic<size_t> TraceableObj::allocated_objs_{0};

using TestTypes = testing::Types<ccb::RefCountReclamation<TraceableObj>,
                                 ccb::EpochBasedReclamation<TraceableObj>,
                                 ccb::HazardPtrReclamation<TraceableObj>>;

template <class RType>
class MemoryReclamationTest : public testing::Test {
 protected:
  MemoryReclamationTest() : ptr_(nullptr) {}

  void SetUp() {
    ASSERT_EQ(0, TraceableObj::allocated_objs());
  }
  void TearDown() {
    ASSERT_EQ(0, TraceableObj::allocated_objs());
  }
  ccb::PtrReclamationAdapter<TraceableObj, RType> recl_;
  std::atomic<TraceableObj*> ptr_;
};
TYPED_TEST_CASE(MemoryReclamationTest, TestTypes);

template <class RType>
class MemoryReclamationPerfTest : public testing::Test {
 protected:
  MemoryReclamationPerfTest() : stop_flag_(false), ptr_(nullptr) {}

  void SetUp() {
    ASSERT_EQ(0, TraceableObj::allocated_objs());
    auto reader_code = [this] {
      while (!stop_flag_.load(std::memory_order_relaxed)) {
        TraceableObj* ptr = this->recl_.ReadLock(&this->ptr_);
        for (int i = 0; ptr && i < 100; i++) {
          ASSERT_EQ(1, ptr->val());
        }
        this->recl_.ReadUnlock();
      }
    };
    for (auto& t : reader_tasks_) {
      t = std::thread(reader_code);
    }
    auto writer_code = [this] {
      while (!stop_flag_.load(std::memory_order_relaxed)) {
        auto old_ptr = this->ptr_.exchange(new TraceableObj);
        if (old_ptr) this->recl_.Retire(old_ptr);
      }
      this->recl_.RetireCleanup();
    };
    for (auto& t : writer_tasks_) {
      t = std::thread(writer_code);
    }
  }
  void TearDown() {
    stop_flag_.store(true);
    for (auto& t : reader_tasks_) {
      t.join();
    }
    for (auto& t : writer_tasks_) {
      t.join();
    }
    this->recl_.Retire(this->ptr_.load());
    this->recl_.RetireCleanup();
    ASSERT_EQ(0, TraceableObj::allocated_objs());
  }
  std::thread reader_tasks_[2];
  std::thread writer_tasks_[1];
  std::atomic<bool> stop_flag_;
  ccb::PtrReclamationAdapter<TraceableObj, RType> recl_;
  std::atomic<TraceableObj*> ptr_;
};
TYPED_TEST_CASE(MemoryReclamationPerfTest, TestTypes);

TYPED_TEST(MemoryReclamationTest, Simple) {
  this->ptr_ = new TraceableObj;
  ASSERT_EQ(1, TraceableObj::allocated_objs());
  TraceableObj* ptr = this->recl_.ReadLock(&this->ptr_);
  ASSERT_EQ(1, ptr->val());
  this->recl_.ReadUnlock();
  ASSERT_EQ(1, TraceableObj::allocated_objs());
  this->ptr_ = nullptr;
  this->recl_.Retire(ptr);
  this->recl_.RetireCleanup();
  ASSERT_EQ(0, TraceableObj::allocated_objs());
}

TYPED_TEST(MemoryReclamationTest, Read) {
  auto deleter = [this] {
    auto ptr = this->ptr_.load();
    this->ptr_ = nullptr;
    this->recl_.Retire(ptr);
    this->recl_.RetireCleanup();
  };
  this->ptr_ = new TraceableObj;
  ASSERT_EQ(1, TraceableObj::allocated_objs());
  auto ptr = this->recl_.ReadLock(&this->ptr_);
  std::thread t{deleter};
  for (int i = 0; i < 10; i++) {
    ASSERT_EQ(1, TraceableObj::allocated_objs());
    ASSERT_EQ(1, ptr->val());
    usleep(10000);
  }
  this->recl_.ReadUnlock();
  t.join();
  ASSERT_EQ(0, TraceableObj::allocated_objs());
}

TYPED_PERF_TEST(MemoryReclamationPerfTest, RetirePerf) {
  auto old_ptr = this->ptr_.exchange(new TraceableObj);
  if (old_ptr) this->recl_.Retire(old_ptr);
}
