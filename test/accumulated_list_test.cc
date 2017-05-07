#include <stdio.h>
#include <atomic>
#include <thread>
#include "gtestx/gtestx.h"
#include "ccbase/accumulated_list.h"

class AccumulatedListTest : public testing::Test {
 protected:
  AccumulatedListTest() {
  }
  void SetUp() {
  }
  void TearDown() {
  }
  static constexpr int magic = 0x37;
  struct TestNode {
    int value{magic};
  };
  ccb::AccumulatedList<TestNode> alist_;
};

constexpr int AccumulatedListTest::magic;

TEST_F(AccumulatedListTest, SingleThread) {
  size_t add_count = 0;
  do {
    size_t travel_count = 0;
    alist_.AddNode(); ++add_count;
    alist_.Travel([&travel_count] (TestNode* n) {
      ASSERT_EQ(magic, n->value);
      ++travel_count;
    });
    ASSERT_EQ(travel_count, add_count);
  } while (add_count < 100);
}

TEST_F(AccumulatedListTest, MultiThread) {
  constexpr size_t loops = 5000000;
  auto w_code = [this] {
    for (size_t i = 0; i < loops; i++) {
      alist_.AddNode();
    }
  };
  std::atomic<bool> r_stop_flag{false};
  auto r_code = [this, &r_stop_flag] {
    while (!r_stop_flag.load(std::memory_order_relaxed)) {
      alist_.Travel([] (TestNode* n) {
        ASSERT_EQ(magic, n->value);
      });
    }
  };
  std::thread w_task1{w_code};
  std::thread w_task2{w_code};
  std::thread w_task3{w_code};
  std::thread r_task1{r_code};
  w_task1.join();
  w_task2.join();
  w_task3.join();
  r_stop_flag.store(true);
  r_task1.join();
  size_t travel_count = 0;
  alist_.Travel([&travel_count] (TestNode*) {
    ++travel_count;
  });
  ASSERT_EQ(loops * 3, travel_count);
}

class TlsAccumulatedListTest : public testing::Test {
 protected:
  TlsAccumulatedListTest() {
  }
  void SetUp() {
  }
  void TearDown() {
  }
  struct TestNode {
    int value{0};
  };
  using TlsList = ccb::TlsAccumulatedList<TestNode, TlsAccumulatedListTest>;
  using TlsList2 = ccb::TlsAccumulatedList<TestNode, TlsList>;
};

TEST_F(TlsAccumulatedListTest, SingleThread) {
  size_t mod_count = 0;
  do {
    size_t travel_count = 0;
    ++(TlsList::TlsNode()->value); ++mod_count;
    TlsList::Travel([&travel_count, mod_count] (TestNode* n) {
      ASSERT_EQ(mod_count, n->value);
      ++travel_count;
    });
    ASSERT_EQ(1, travel_count);
  } while (mod_count < 100);
}

TEST_F(TlsAccumulatedListTest, MultiThread) {
  constexpr size_t task_num = 1000;
  std::thread tasks[task_num];
  for (auto& task : tasks) {
    task = std::thread([] {
      TlsList2::TlsNode()->value++;
    });
  }
  for (auto& task : tasks) {
    task.join();
  }
  size_t travel_count = 0;
  TlsList2::Travel([&travel_count] (TestNode*) {
    ++travel_count;
  });
  ASSERT_EQ(task_num, travel_count);
}

