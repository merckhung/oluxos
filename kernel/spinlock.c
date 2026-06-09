#include <kernel/spinlock.h>

void spin_init(Spinlock* lock) {
    lock->locked = 0;
}

void spin_lock(Spinlock* lock) {
    while (__sync_lock_test_and_set(&lock->locked, 1)) {
        #if defined(CONFIG_ARCH_RISCV64) || defined(CONFIG_ARCH_RISCV32)
        __asm__ volatile("nop");
        #elif defined(CONFIG_ARCH_ARM64)
        __asm__ volatile("yield");
        #endif
    }
}

void spin_unlock(Spinlock* lock) {
    __sync_lock_release(&lock->locked);
}
