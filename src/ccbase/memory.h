#ifndef _MEMORY_H
#define _MEMORY_H

#include "common.h"

namespace ccb {

#if __GNUC__
# if defined __i386__ || defined __x86_64__

static inline void MemoryBarrier() {
  __asm__ __volatile__("mfence" ::: "memory");
}

static inline void MemoryReadBarrier() {
     __asm__ __volatile__("lfence" ::: "memory");
}

static inline void MemoryWriteBarrier() {
     __asm__ __volatile__("sfence" ::: "memory");
}

# endif
#endif


} // namespace ccb

#endif
