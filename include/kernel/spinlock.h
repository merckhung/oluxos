#ifndef __KERNEL_SPINLOCK_H__
#define __KERNEL_SPINLOCK_H__

#include <types.h>

typedef struct Spinlock {
    volatile uint32_t locked;
} Spinlock;

void spin_init(Spinlock* lock);
void spin_lock(Spinlock* lock);
void spin_unlock(Spinlock* lock);

#endif // __KERNEL_SPINLOCK_H__
