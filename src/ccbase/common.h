#ifndef _CCB_COMMON_H
#define _CCB_COMMON_H

#include <stdint.h>
#include <assert.h>
#include <stdio.h>
#include <stdexcept>

#define NOT_COPYABLE_AND_MOVABLE(ClassName) \
  ClassName(const ClassName&) = delete; \
  void operator=(const ClassName&) = delete; \
  ClassName(ClassName&&) = delete; \
  void operator=(ClassName&&) = delete

#endif // _CCB_COMMON_H
