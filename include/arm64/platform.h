#ifndef __ARM64_PLATFORM_H__
#define __ARM64_PLATFORM_H__

#include <types.h>

typedef struct PACKED _ARM64Registers {
    uint64_t x[31];
    uint64_t sp;
    uint64_t pc;
    uint64_t spsr;
} ARM64Registers;

#endif // __ARM64_PLATFORM_H__
