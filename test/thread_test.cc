#include <unistd.h>
#include <stdlib.h>
#include <atomic>
#include "gtestx/gtestx.h"
#include "ccbase/thread.h"

TEST(Thread, Test)
{
  std::atomic_int i{4};
  ccb::CreateDetachedThread([&i] {
      i--;
  });
  ccb::CreateDetachedThread("detached-thread", [&i] {
      i--;
  });
  usleep(10000);
  std::thread t1 = ccb::CreateThread([&i] {
      i--;
  });
  std::thread t2 = ccb::CreateThread("thread", [&i] {
      i--;
  });
  t1.join();
  t2.join();
  ASSERT_EQ(0, i);
}
