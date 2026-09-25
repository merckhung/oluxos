#ifndef OLUX_TYPES_H
#define OLUX_TYPES_H

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;

typedef u64 phys_addr_t;
typedef u64 vaddr_t;
typedef long ssize_t;
typedef s64 off_t;
typedef s64 loff_t;
typedef u32 mode_t;
typedef s32 pid_t;
typedef u32 uid_t;
typedef u32 gid_t;
typedef u64 ino_t;
typedef u64 dev_t;
typedef s64 time_t;

#endif
