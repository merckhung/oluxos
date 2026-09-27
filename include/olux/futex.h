#ifndef OLUX_FUTEX_H
#define OLUX_FUTEX_H

#include <olux/types.h>

long futex_wait(u64 uaddr, u32 val, long timeout_ns, u32 bitset, bool private);
long futex_wake(u64 uaddr, int n);
long futex_wake_bitset(u64 uaddr, int n, u32 bitset, bool private);
long futex_requeue(u64 uaddr, int nwake, u64 uaddr2, int nrequeue, bool cmp, u32 cmpval, bool private);

#endif
