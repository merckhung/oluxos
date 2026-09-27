#ifndef OLUX_SPINLOCK_H
#define OLUX_SPINLOCK_H

#include <asm/irqflags.h>
#include <asm/sysreg.h>
#include <olux/types.h>

/*
 * Fair ticket spinlock. Waiters sleep in WFE; the unlocking CPU issues SEV.
 * Spinlocks never sleep; use the _irqsave variants for any lock that is also
 * taken from interrupt context.
 */
typedef struct {
  u16 owner;
  u16 next;
  s32 cpu; /* holder, for debugging */
} spinlock_t;

#define SPINLOCK_INIT {0, 0, -1}
#define DEFINE_SPINLOCK(x) spinlock_t x = SPINLOCK_INIT

static inline void spin_lock_init(spinlock_t *l) {
  l->owner = l->next = 0;
  l->cpu = -1;
}

void spin_lock(spinlock_t *l);
void spin_unlock(spinlock_t *l);
bool spin_trylock(spinlock_t *l);
bool spin_is_locked(spinlock_t *l);

static inline unsigned long spin_lock_irqsave(spinlock_t *l) {
  unsigned long f = local_irq_save();
  spin_lock(l);
  return f;
}

static inline void spin_unlock_irqrestore(spinlock_t *l, unsigned long f) {
  spin_unlock(l);
  local_irq_restore(f);
}

/* Atomics (C11 builtins; compiled to LL/SC or LSE depending on -march). */
typedef struct {
  volatile long v;
} atomic_t;

#define ATOMIC_INIT(x) {(x)}
static inline long atomic_read(const atomic_t *a) { return __atomic_load_n(&a->v, __ATOMIC_RELAXED); }
static inline void atomic_set(atomic_t *a, long v) { __atomic_store_n(&a->v, v, __ATOMIC_RELAXED); }
static inline long atomic_add_return(atomic_t *a, long v) { return __atomic_add_fetch(&a->v, v, __ATOMIC_SEQ_CST); }
static inline long atomic_inc_return(atomic_t *a) { return atomic_add_return(a, 1); }
static inline long atomic_dec_return(atomic_t *a) { return atomic_add_return(a, -1); }
static inline void atomic_inc(atomic_t *a) { atomic_add_return(a, 1); }
static inline void atomic_dec(atomic_t *a) { atomic_add_return(a, -1); }
static inline bool atomic_cmpxchg(atomic_t *a, long old, long nv) {
  return __atomic_compare_exchange_n(&a->v, &old, nv, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}

#endif
