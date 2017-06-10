#include <unistd.h>
#include <atomic>
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
    auto reader_code = [this] {
      while (!stop_flag_.load(std::memory_order_relaxed)) {
        typename decltype(this->conc_ptr_)::Reader reader(&this->conc_ptr_);
        if (!reader.get()) continue;
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
      this->conc_ptr_.Reset(true);
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
    this->conc_ptr_.Reset(true);
    ConcurrentPtrTest<RType>::TearDown();
  }

  std::thread reader_tasks_[2];
  std::thread writer_tasks_[1];
  std::atomic<bool> stop_flag_{false};
};
TYPED_TEST_CASE(ConcurrentPtrPerfTest, TestTypes<TraceableObj>);

TYPED_PERF_TEST(ConcurrentPtrPerfTest, ResetPerf) {
  this->conc_ptr_.Reset(new TraceableObj);
}

TYPED_PERF_TEST(ConcurrentPtrPerfTest, ReaderPerf) {
  typename decltype(this->conc_ptr_)::Reader reader(&this->conc_ptr_);
  if (reader.get()) {
    ASSERT_EQ(1, reader->val()) << PERF_ABORT;
  }
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


