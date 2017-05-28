#include <unistd.h>
#include <atomic>
#include <thread>
#include <type_traits>
#include "gtestx/gtestx.h"
#include "ccbase/memory_reclamation.h"

struct SomeType {
  SomeType() : val(1) {}
  ~SomeType() { val = 0; }

  static void* operator new(size_t sz) {
    allocated++;
    return ::operator new(sz);
  }
  static void operator delete(void* ptr, size_t sz) {
    allocated--;
    ::operator delete(ptr);
  }
  int val;
  static std::atomic<size_t> allocated;
};

std::atomic<size_t> SomeType::allocated{0};

using TestTypes = testing::Types<ccb::RefCountReclamation<SomeType>,
                                 ccb::EpochBasedReclamation<SomeType>>;

template <class RType>
class MemoryReclamationTest : public testing::Test {
 protected:
  void SetUp() {
    ASSERT_EQ(0, SomeType::allocated);
  }
  void TearDown() {
    ASSERT_EQ(0, SomeType::allocated);
  }
  RType recl_;
  SomeType* ptr_{nullptr};
};
TYPED_TEST_CASE(MemoryReclamationTest, TestTypes);

template <class RType>
class MemoryReclamationPerfTest : public MemoryReclamationTest<RType> {
 protected:
  void SetUp() {
    MemoryReclamationTest<RType>::SetUp();
    auto read_code = [this] {
      while (!stop_flag_.load(std::memory_order_relaxed)) {
        this->recl_.ReadLock();
        for (int i = 0; this->ptr_ && i < 100; i++) {
          ASSERT_EQ(1, this->ptr_->val);
        }
        this->recl_.ReadUnlock();
      }
    };
    for (auto& t : read_tasks_) {
      t = std::thread(read_code);
    }
  }
  void TearDown() {
    stop_flag_.store(true);
    for (auto& t : read_tasks_) {
      t.join();
    }
    this->recl_.Retire(this->ptr_);
    this->recl_.RetireCleanup();
    MemoryReclamationTest<RType>::TearDown();
  }
  std::thread read_tasks_[2];
  std::atomic<bool> stop_flag_{false};
};
TYPED_TEST_CASE(MemoryReclamationPerfTest, TestTypes);

TYPED_TEST(MemoryReclamationTest, Simple) {
  this->ptr_ = new SomeType;
  ASSERT_EQ(1, SomeType::allocated);
  this->recl_.ReadLock();
  ASSERT_EQ(1, this->ptr_->val);
  this->recl_.ReadUnlock();
  ASSERT_EQ(1, SomeType::allocated);
  auto ptr = this->ptr_;
  this->ptr_ = nullptr;
  this->recl_.Retire(ptr);
  this->recl_.RetireCleanup();
  ASSERT_EQ(0, SomeType::allocated);
}

TYPED_TEST(MemoryReclamationTest, Read) {
  auto deleter = [this] {
    auto ptr = this->ptr_;
    this->ptr_ = nullptr;
    this->recl_.Retire(ptr);
    this->recl_.RetireCleanup();
  };
  this->ptr_ = new SomeType;
  ASSERT_EQ(1, SomeType::allocated);
  this->recl_.ReadLock();
  auto ptr = this->ptr_;
  std::thread t{deleter};
  for (int i = 0; i < 10; i++) {
    ASSERT_EQ(1, SomeType::allocated);
    ASSERT_EQ(1, ptr->val);
    usleep(10000);
  }
  this->recl_.ReadUnlock();
  t.join();
  ASSERT_EQ(0, SomeType::allocated);
}

TYPED_PERF_TEST(MemoryReclamationPerfTest, RetirePerf) {
  auto old_ptr = this->ptr_;
  this->ptr_ = new SomeType;
  if (old_ptr) this->recl_.Retire(old_ptr);
}
