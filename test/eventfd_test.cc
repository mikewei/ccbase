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

