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
#include <memory>
#include <thread>
#include "gtestx/gtestx.h"
#include "ccbase/concurrent_ptr.h"

namespace {

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

template <class T, class ST = ccb::ConcurrentPtrScope<T>>
using TestTypes = testing::Types<ccb::RefCountReclamation<T>,
                                 ccb::EpochBasedReclamation<T, ST>,
                                 ccb::HazardPtrReclamation<T, ST>>;

}  // namespace

template <class RType>
class ConcurrentPtrTest : public testing::Test {
 protected:
  void SetUp() {
    ASSERT_EQ(0, TraceableObj::allocated_objs());
  }
  void TearDown() {
    ASSERT_EQ(0, TraceableObj::allocated_objs());
  }

  ccb::ConcurrentPtr<TraceableObj,
                     std::default_delete<TraceableObj>,
                     RType> conc_ptr_;
};
TYPED_TEST_CASE(ConcurrentPtrTest, TestTypes<TraceableObj>);

TYPED_TEST(ConcurrentPtrTest, ReadLock) {
  TraceableObj* ptr = new TraceableObj;
  this->conc_ptr_.Reset(ptr);
  ASSERT_EQ(1, TraceableObj::allocated_objs());
  TraceableObj* rp = this->conc_ptr_.ReadLock();
  ASSERT_EQ(ptr, rp);
  ASSERT_EQ(1, rp->val());
  this->conc_ptr_.ReadUnlock();
  this->conc_ptr_.Reset(true);
}

TYPED_TEST(ConcurrentPtrTest, Reader) {
  TraceableObj* ptr = new TraceableObj;
  this->conc_ptr_.Reset(ptr);
  ASSERT_EQ(1, TraceableObj::allocated_objs());
  {
    typename decltype(this->conc_ptr_)::Reader reader(&this->conc_ptr_);
    ASSERT_EQ(ptr, reader.get());
    ASSERT_EQ(1, reader->val());
  }
  this->conc_ptr_.Reset(true);
}

TYPED_TEST(ConcurrentPtrTest, Reset) {
  this->conc_ptr_.Reset(new TraceableObj);
  TraceableObj* rp = this->conc_ptr_.ReadLock();
  ASSERT_EQ(1, rp->val());
  this->conc_ptr_.ReadUnlock();
  this->conc_ptr_.Reset(new TraceableObj);
  rp = this->conc_ptr_.ReadLock();
  ASSERT_EQ(1, rp->val());
  this->conc_ptr_.ReadUnlock();
  this->conc_ptr_.Reset(true);
}

template <class RType>
class ConcurrentPtrPerfTest : public ConcurrentPtrTest<RType> {
 protected:
  void SetUp() {
    ConcurrentPtrTest<RType>::SetUp();
    this->conc_ptr_.Reset(new TraceableObj);
    auto reader_code = [this] {
      while (!stop_flag_.load(std::memory_order_relaxed)) {
        typename decltype(this->conc_ptr_)::Reader reader(&this->conc_ptr_);
        for (int i = 0; i < 100; i++) {
          ASSERT_EQ(1, reader->val());
        }
      }
    };
    for (auto& t : reader_tasks_) {
      t = std::thread(reader_code);
    }
    auto writer_code = [this] {
      while (!stop_flag_.load(std::memory_order_relaxed)) {
        this->conc_ptr_.Reset(new TraceableObj);
      }
      this->conc_ptr_.Reset(new TraceableObj, true);
    };
    for (auto& t : writer_tasks_) {
      t = std::thread(writer_code);
    }
    auto spawn_code = [this] {
      while (!stop_flag_.load(std::memory_order_relaxed)) {
        std::thread([this] {
          typename decltype(this->conc_ptr_)::Reader reader(&this->conc_ptr_);
          for (int i = 0; i < 100; i++) {
            ASSERT_EQ(1, reader->val());
          }
        }).join();
      }
    };
    for (auto& t : spawn_tasks_) {
      t = std::thread(spawn_code);
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
    for (auto& t : spawn_tasks_) {
      t.join();
    }
    this->conc_ptr_.Reset(true);
    ConcurrentPtrTest<RType>::TearDown();
  }

  std::thread reader_tasks_[1];
  std::thread writer_tasks_[1];
  std::thread spawn_tasks_[1];
  std::atomic<bool> stop_flag_{false};
};
TYPED_TEST_CASE(ConcurrentPtrPerfTest, TestTypes<TraceableObj>);

TYPED_PERF_TEST(ConcurrentPtrPerfTest, ResetPerf) {
  this->conc_ptr_.Reset(new TraceableObj);
}

TYPED_PERF_TEST(ConcurrentPtrPerfTest, ReaderPerf) {
  typename decltype(this->conc_ptr_)::Reader reader(&this->conc_ptr_);
  ASSERT_EQ(1, reader->val()) << PERF_ABORT;
}

template <class RType>
class ConcurrentSharedPtrTest : public testing::Test {
 protected:
  void SetUp() {
    ASSERT_EQ(0, TraceableObj::allocated_objs());
  }
  void TearDown() {
    ASSERT_EQ(0, TraceableObj::allocated_objs());
  }

  ccb::ConcurrentSharedPtr<TraceableObj,
                           std::default_delete<TraceableObj>,
                           RType> cs_ptr_;
};
TYPED_TEST_CASE(ConcurrentSharedPtrTest, TestTypes<std::shared_ptr<TraceableObj>>);

TYPED_TEST(ConcurrentSharedPtrTest, Read) {
  TraceableObj* ptr = new TraceableObj;
  this->cs_ptr_.Reset(ptr);
  ASSERT_EQ(1, TraceableObj::allocated_objs());
  std::shared_ptr<TraceableObj> rp = this->cs_ptr_.Get();
  ASSERT_EQ(ptr, rp.get());
  ASSERT_EQ(1, rp->val());
  ASSERT_EQ(1, this->cs_ptr_->val());
  this->cs_ptr_.Reset(true);
}

TYPED_TEST(ConcurrentSharedPtrTest, Reset) {
  this->cs_ptr_.Reset(new TraceableObj);
  std::shared_ptr<TraceableObj> rp = this->cs_ptr_.Get();
  ASSERT_EQ(1, rp->val());
  this->cs_ptr_.Reset(std::make_shared<TraceableObj>());
  ASSERT_EQ(1, this->cs_ptr_->val());
  ASSERT_NE(rp, this->cs_ptr_.Get());
  this->cs_ptr_.Reset(true);
}

template <class RType>
class ConcurrentSharedPtrPerfTest : public ConcurrentSharedPtrTest<RType> {
 protected:
  void SetUp() {
    ConcurrentSharedPtrTest<RType>::SetUp();
    this->cs_ptr_.Reset(new TraceableObj);
    auto reader_code = [this] {
      while (!stop_flag_.load(std::memory_order_relaxed)) {
        std::shared_ptr<TraceableObj> ptr = this->cs_ptr_.Get();
        for (int i = 0; i < 100; i++) {
          ASSERT_EQ(1, ptr->val());
        }
      }
    };
    for (auto& t : reader_tasks_) {
      t = std::thread(reader_code);
    }
    auto writer_code = [this] {
      while (!stop_flag_.load(std::memory_order_relaxed)) {
        this->cs_ptr_.Reset(new TraceableObj);
      }
      this->cs_ptr_.Reset(new TraceableObj, true);
    };
    for (auto& t : writer_tasks_) {
      t = std::thread(writer_code);
    }
    auto spawn_code = [this] {
      while (!stop_flag_.load(std::memory_order_relaxed)) {
        std::thread([this] {
          std::shared_ptr<TraceableObj> ptr = this->cs_ptr_.Get();
          for (int i = 0; i < 100; i++) {
            ASSERT_EQ(1, ptr->val());
          }
        }).join();
      }
    };
    for (auto& t : spawn_tasks_) {
      t = std::thread(spawn_code);
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
    for (auto& t : spawn_tasks_) {
      t.join();
    }
    this->cs_ptr_.Reset(true);
    ConcurrentSharedPtrTest<RType>::TearDown();
  }

  std::thread reader_tasks_[1];
  std::thread writer_tasks_[1];
  std::thread spawn_tasks_[1];
  std::atomic<bool> stop_flag_{false};
};
TYPED_TEST_CASE(ConcurrentSharedPtrPerfTest, TestTypes<std::shared_ptr<TraceableObj>>);

TYPED_PERF_TEST(ConcurrentSharedPtrPerfTest, ResetPerf) {
  this->cs_ptr_.Reset(new TraceableObj);
}

TYPED_PERF_TEST(ConcurrentSharedPtrPerfTest, ReaderPerf) {
  ASSERT_EQ(1, this->cs_ptr_->val()) << PERF_ABORT;
}

// atomic std::shared_ptr is not available below gcc-5.0
#if defined(__GNUC__) && __GNUC__ >= 5
class StdAtomicSharedPtrPerfTest : public testing::Test {
 protected:
  void SetUp() {
    std::atomic_store(&this->as_ptr_, std::make_shared<TraceableObj>());
    auto reader_code = [this] {
      while (!stop_flag_.load(std::memory_order_relaxed)) {
        std::shared_ptr<TraceableObj> ptr = std::atomic_load(&this->as_ptr_);
        for (int i = 0; i < 100; i++) {
          ASSERT_EQ(1, ptr->val());
        }
      }
    };
    for (auto& t : reader_tasks_) {
      t = std::thread(reader_code);
    }
    auto writer_code = [this] {
      while (!stop_flag_.load(std::memory_order_relaxed)) {
        std::atomic_store(&this->as_ptr_, std::make_shared<TraceableObj>());
      }
    };
    for (auto& t : writer_tasks_) {
      t = std::thread(writer_code);
    }
    auto spawn_code = [this] {
      while (!stop_flag_.load(std::memory_order_relaxed)) {
        std::thread([this] {
          std::shared_ptr<TraceableObj> ptr = std::atomic_load(&this->as_ptr_);
          for (int i = 0; i < 100; i++) {
            ASSERT_EQ(1, ptr->val());
          }
        }).join();
      }
    };
    for (auto& t : spawn_tasks_) {
      t = std::thread(spawn_code);
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
    for (auto& t : spawn_tasks_) {
      t.join();
    }
  }

  std::thread reader_tasks_[1];
  std::thread writer_tasks_[1];
  std::thread spawn_tasks_[1];
  std::atomic<bool> stop_flag_{false};
  std::shared_ptr<TraceableObj> as_ptr_;
};

PERF_TEST_F(StdAtomicSharedPtrPerfTest, ResetPerf) {
  std::atomic_store(&this->as_ptr_, std::make_shared<TraceableObj>());
}

PERF_TEST_F(StdAtomicSharedPtrPerfTest, ReaderPerf) {
  ASSERT_EQ(1, std::atomic_load(&this->as_ptr_)->val()) << PERF_ABORT;
}
#endif

