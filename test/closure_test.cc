#include <functional>
#include "gtestx/gtestx.h"
#include "ccbase/closure.h"

class ClosureTest : public testing::Test
{
public:
  ClosureTest() {
  }
  void SetUp() {
  }
  void TearDown() {
  }
  static void Function() {
    n++;
  }
  void Method() {
    n++;
  }
  void Method_Args(int a, int b) {
    n++;
  }
  struct Functor {
    void operator()() const {
      n++;
    }
    void operator()(int) const {
      n++;
    }
  };
  static int n;
};

class ClosureFuncTest : public ClosureTest
{
};

int ClosureTest::n = 0;

TEST_F(ClosureTest, Run) {
  int expect = 0; n = 0;
  // NewClosure
  ccb::NewClosure(ClosureTest::Function)->Run();
  ASSERT_EQ(++expect, n);
  ccb::NewClosure(static_cast<ClosureTest*>(this), &ClosureTest::Method)->Run();
  ASSERT_EQ(++expect, n);
  ccb::NewClosure(static_cast<ClosureTest*>(this), &ClosureTest::Method_Args, 1)->Run(-1);
  ASSERT_EQ(++expect, n);
  ccb::NewClosure(Functor())->Run();
  ASSERT_EQ(++expect, n);
  ccb::NewClosure([]{ClosureTest::n++;})->Run();
  ASSERT_EQ(++expect, n);
  ccb::NewClosure(std::bind(&ClosureTest::Method, static_cast<ClosureTest*>(this)))->Run();
  ASSERT_EQ(++expect, n);
  // NewPermanentClosure
  ccb::Closure<void()>* ptr;
  (ptr = ccb::NewPermanentClosure(ClosureTest::Function))->Run(); delete ptr;
  ASSERT_EQ(++expect, n);
  (ptr = ccb::NewPermanentClosure(static_cast<ClosureTest*>(this), &ClosureTest::Method))->Run(); delete ptr;
  ASSERT_EQ(++expect, n);
  (ptr = ccb::NewPermanentClosure(static_cast<ClosureTest*>(this), &ClosureTest::Method_Args, 1, -1))->Run(); delete ptr;
  ASSERT_EQ(++expect, n);
  (ptr = ccb::NewPermanentClosure(Functor()))->Run(); delete ptr;
  ASSERT_EQ(++expect, n);
  (ptr = ccb::NewPermanentClosure([]{ClosureTest::n++;}))->Run(); delete ptr;
  ASSERT_EQ(++expect, n);
  (ptr = ccb::NewPermanentClosure(std::bind(&ClosureTest::Method, static_cast<ClosureTest*>(this))))->Run(); delete ptr;
  ASSERT_EQ(++expect, n);
}

TEST_F(ClosureTest, Clone) {
  int expect = 0; n = 0;
  ccb::Closure<void()>* ptr;
  ccb::Closure<void()>* ptr2;
  // NewClosure
  ccb::NewClosure(ClosureTest::Function)->Run();
  ASSERT_EQ(++expect, n);
  ptr = ccb::NewClosure(static_cast<ClosureTest*>(this), &ClosureTest::Method);
  ptr->Clone()->Run();
  ptr->Run();
  ASSERT_EQ(++++expect, n);
  // NewPermanentClosure
  ptr = ccb::NewPermanentClosure(static_cast<ClosureTest*>(this), &ClosureTest::Method_Args, 1, -1);
  ptr2 = ptr->Clone();
  ptr->Run(); delete ptr;
  ptr2->Run(); delete ptr2;
  ASSERT_EQ(++++expect, n);
}

PERF_TEST_F(ClosureTest, Perf) {
  ccb::NewClosure(static_cast<ClosureTest*>(this), &ClosureTest::Method_Args, 1)->Run(-1);
}

TEST_F(ClosureFuncTest, Run) {
  int expect = 0; n = 0;
  ccb::BindClosure(ClosureFuncTest::Function)();
  ASSERT_EQ(++expect, n);
  ccb::BindClosure(static_cast<ClosureFuncTest*>(this), &ClosureFuncTest::Method)();
  ASSERT_EQ(++expect, n);
  ccb::BindClosure(static_cast<ClosureFuncTest*>(this), &ClosureFuncTest::Method_Args, 1)(-1);
  ASSERT_EQ(++expect, n);
  ccb::BindClosure(Functor())();
  ASSERT_EQ(++expect, n);
  ccb::BindClosure<void, int>(Functor())(0);
  ASSERT_EQ(++expect, n);
  ccb::BindClosure([]{ClosureFuncTest::n++;})();
  ASSERT_EQ(++expect, n);
  ccb::BindClosure<int, int>([](int)->int{return ClosureFuncTest::n++;})(0);
  ASSERT_EQ(++expect, n);
  ccb::BindClosure(std::bind(&ClosureFuncTest::Method, static_cast<ClosureFuncTest*>(this)))();
  ASSERT_EQ(++expect, n);
}

TEST_F(ClosureFuncTest, Ops) {
  ccb::ClosureFunc<void()> f{[]{}};
  ASSERT_TRUE(f);
  f.reset();
  ASSERT_FALSE(f);
  ccb::ClosureFunc<void()>([]{}).swap(f);
  ASSERT_TRUE(f);
  ccb::ClosureFunc<void()> f2{[]{}};
  f = f2;
  ASSERT_TRUE(f);
  (f = f2).reset();
  ASSERT_FALSE(f);
}

TEST_F(ClosureFuncTest, OpsArg1) {
  ccb::ClosureFunc<void(int)> f{[](int){}};
  ASSERT_TRUE(f);
  f.reset();
  ASSERT_FALSE(f);
  ccb::ClosureFunc<void(int)>([](int){}).swap(f);
  ASSERT_TRUE(f);
  ccb::ClosureFunc<void(int)> f2{[](int){}};
  f = f2;
  ASSERT_TRUE(f);
  (f = f2).reset();
  ASSERT_FALSE(f);
}

TEST_F(ClosureFuncTest, OpsArg2) {
  ccb::ClosureFunc<void(int, std::string)> f{[](int, std::string){}};
  ASSERT_TRUE(f);
  f.reset();
  ASSERT_FALSE(f);
  ccb::ClosureFunc<void(int, std::string)>([](int, std::string){}).swap(f);
  ASSERT_TRUE(f);
  ccb::ClosureFunc<void(int, std::string)> f2{[](int, std::string){}};
  f = f2;
  ASSERT_TRUE(f);
  (f = f2).reset();
  ASSERT_FALSE(f);
}

TEST_F(ClosureFuncTest, CopyMove) {
  int n = 0;
  ccb::ClosureFunc<int()> f{[n]()mutable{return ++n;}};
  ASSERT_TRUE(f);
  ASSERT_EQ(1, f());
  ccb::ClosureFunc<int()> f2{f};
  ASSERT_TRUE(f2);
  ASSERT_EQ(2, f2());
  ccb::ClosureFunc<int()> f3{std::move(f2)};
  ASSERT_TRUE(f3);
  ASSERT_FALSE(f2);
  ASSERT_EQ(3, f3());
  ASSERT_EQ(4, f());
}

PERF_TEST_F(ClosureFuncTest, NewCall) {
  ccb::ClosureFunc<void()>{[]{}}();
}

PERF_TEST_F(ClosureFuncTest, NewMoveCall) {
  ccb::ClosureFunc<void()> f{[]{}};
  ccb::ClosureFunc<void()>{std::move(f)}();
}

PERF_TEST_F(ClosureFuncTest, CopyCall) {
  static ccb::ClosureFunc<void()> f = {[]{}};
  ccb::ClosureFunc<void()>{f}();
}

