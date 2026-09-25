#include <olux/kernel.h>
#include <olux/smp.h>
#include <olux/spinlock.h>

void spin_lock(spinlock_t *l) {
  u16 ticket = __atomic_fetch_add(&l->next, 1, __ATOMIC_RELAXED);
  while (__atomic_load_n(&l->owner, __ATOMIC_ACQUIRE) != ticket) wfe();
  l->cpu = smp_processor_id();
}

bool spin_trylock(spinlock_t *l) {
  u32 cur = __atomic_load_n((u32 *)l, __ATOMIC_RELAXED);
  u16 owner = cur & 0xffff, next = cur >> 16;
  if (owner != next) return false;
  u32 want = ((u32)(u16)(next + 1) << 16) | owner;
  if (!__atomic_compare_exchange_n((u32 *)l, &cur, want, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
    return false;
  l->cpu = smp_processor_id();
  return true;
}

void spin_unlock(spinlock_t *l) {
  l->cpu = -1;
  __atomic_store_n(&l->owner, (u16)(l->owner + 1), __ATOMIC_RELEASE);
  dsb(ishst);
  sev();
}

bool spin_is_locked(spinlock_t *l) {
  return __atomic_load_n(&l->owner, __ATOMIC_RELAXED) != __atomic_load_n(&l->next, __ATOMIC_RELAXED);
}
