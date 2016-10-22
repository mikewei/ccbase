#include "gtestx/gtestx.h"
#include "ccbase/eventfd.h"

TEST(EventFd, Simple)
{
  ccb::EventFd efd;
  ASSERT_FALSE(efd.Get());
  ASSERT_TRUE(efd.Notify());
  ASSERT_TRUE(efd.Get());
  ASSERT_FALSE(efd.GetWait(10));
}

PERF_TEST(EventFd, ConstructPerf)
{
  ccb::EventFd efd;
}

PERF_TEST(EventFd, NotifyAndGet)
{
  static ccb::EventFd efd;
  efd.Notify();
  efd.Get();
}

PERF_TEST(EventFd, CtorNotifyAndGet)
{
  ccb::EventFd efd;
  efd.Notify();
  efd.Get();
}
