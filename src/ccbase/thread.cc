#include <sys/prctl.h>
#include "ccbase/thread.h"

namespace ccb {

static void SetThreadName(const std::string& name)
{
  if (name.size() > 0) {
    char buf[17];
    prctl(PR_GET_NAME, buf, 0, 0, 0);
    buf[16] = 0;
    std::string str{buf};
    str.append("/");
    str.append(name);
    prctl(PR_SET_NAME, str.c_str(), 0, 0, 0);
  }
}

std::thread CreateThread(ClosureFunc<void()> func)
{
  return std::thread(std::move(func));
}

std::thread CreateThread(const std::string& name, ClosureFunc<void()> func)
{
  return std::thread([name, func] {
    SetThreadName(name);
    func();
  });
}

void CreateDetachedThread(ClosureFunc<void()> func)
{
  std::thread(std::move(func)).detach();
}

void CreateDetachedThread(const std::string& name, ClosureFunc<void()> func)
{
  std::thread([name, func] {
    SetThreadName(name);
    func();
  }).detach();
}

}
