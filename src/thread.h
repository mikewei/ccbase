#ifndef _CCB_THREAD_H
#define _CCB_THREAD_H

#include <thread>
#include "ccbase/common.h"
#include "ccbase/closure.h"

namespace ccb {

std::thread CreateThread(ClosureFunc<void()> func);
std::thread CreateThread(const std::string& name, ClosureFunc<void()> func);
void CreateDetachedThread(ClosureFunc<void()> func);
void CreateDetachedThread(const std::string& name, ClosureFunc<void()> func);

}

#endif
