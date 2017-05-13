#include <stdio.h>
#include <atomic>
#include <memory>
#include <vector>
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

class AllocatedListTest : public testing::Test {
 protected:
  AllocatedListTest() {
  }
  void SetUp() {
  }
  void TearDown() {
  }
  static constexpr int magic = 0x37;
  struct TestNode {
    int value{magic};
  };
  ccb::AllocatedList<TestNode> alist_;
};

constexpr int AllocatedListTest::magic;

TEST_F(AllocatedListTest, SingleThread) {
  std::vector<TestNode*> added_nodes;
  do {
    added_nodes.push_back(alist_.Alloc());
    size_t travel_count = 0;
    alist_.Travel([&travel_count] (TestNode* n) {
      ASSERT_EQ(magic, n->value);
      ++travel_count;
    });
    ASSERT_EQ(travel_count, added_nodes.size());
  } while (added_nodes.size() < 100);
  while (added_nodes.size() > 0) {
    alist_.Free(added_nodes.back());
    added_nodes.pop_back();
    size_t travel_count = 0;
    alist_.Travel([&travel_count] (TestNode* n) {
      ASSERT_EQ(magic, n->value);
      ++travel_count;
    });
    ASSERT_EQ(travel_count, added_nodes.size());
  }
}

TEST_F(AllocatedListTest, MultiThread) {
  auto code = [this](int batch) {
    std::vector<TestNode*> added_nodes;
    for (int n = 0; n < 10000000/batch; n++) {
      for (int i = 0; i < batch; i++) {
        added_nodes.push_back(alist_.Alloc());
      }
      for (int i = 0; i < batch; i++) {
        alist_.Free(added_nodes[i]);
      }
      added_nodes.clear();
    }
    alist_.Alloc();
  };
  std::thread task1{code, 1};
  std::thread task2{code, 2};
  std::thread task3{code, 3};
  task1.join();
  task2.join();
  task3.join();
  size_t travel_count = 0;
  alist_.Travel([&travel_count] (TestNode*) {
    ++travel_count;
  });
  ASSERT_EQ(3, travel_count);
}

class ThreadLocalListTest : public testing::Test {
 protected:
  ThreadLocalListTest() {
  }
  void SetUp() {
  }
  void TearDown() {
  }
  struct TestNode {
    TestNode() : value(0) {}
    ~TestNode() {value = -1;}
    int value;
  };
  template <int N> struct ScopeTag {};
  ccb::ThreadLocalList<TestNode, ScopeTag<0>> alist_;
  ccb::ThreadLocalList<TestNode, ScopeTag<1>> alist_mt_;
};

TEST_F(ThreadLocalListTest, SingleThread) {
  size_t mod_count = 0;
  do {
    size_t travel_count = 0;
    ++(alist_.LocalNode()->value); ++mod_count;
    alist_.Travel([&travel_count, mod_count] (TestNode* n) {
      ASSERT_EQ(mod_count, n->value);
      ++travel_count;
    });
    ASSERT_EQ(1, travel_count);
  } while (mod_count < 100);
}

TEST_F(ThreadLocalListTest, MultiThread) {
  constexpr size_t task_num = 1000;
  std::thread tasks[task_num];
  for (auto& task : tasks) {
    task = std::thread([this] {
      alist_mt_.LocalNode()->value++;
      ASSERT_EQ(1, alist_mt_.LocalNode()->value);
    });
  }
  for (auto& task : tasks) {
    task.join();
  }
  size_t travel_count = 0;
  alist_mt_.Travel([&travel_count] (TestNode*) {
    ++travel_count;
  });
  ASSERT_EQ(0, travel_count);
}

