#ifndef __ARM64_PLATFORM_H__
#define __ARM64_PLATFORM_H__

#include <types.h>

typedef struct PACKED _ARM64Registers {
    u64 x[31];
    u64 sp;
    u64 pc;
    u64 spsr;
} ARM64Registers;

#endif // __ARM64_PLATFORM_H__
