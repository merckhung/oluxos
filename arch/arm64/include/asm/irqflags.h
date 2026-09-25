#ifndef ASM_IRQFLAGS_H
#define ASM_IRQFLAGS_H

#include <olux/types.h>

/* Only the I bit is used for interrupt masking; FIQs are unused. */
static inline unsigned long local_irq_save(void) {
  unsigned long flags;
  __asm__ volatile("mrs %0, daif\n msr daifset, #2" : "=r"(flags)::"memory");
  return flags;
}

static inline void local_irq_restore(unsigned long flags) {
  __asm__ volatile("msr daif, %0" ::"r"(flags) : "memory");
}

static inline void local_irq_enable(void) { __asm__ volatile("msr daifclr, #2" ::: "memory"); }
static inline void local_irq_disable(void) { __asm__ volatile("msr daifset, #2" ::: "memory"); }

static inline bool irqs_disabled(void) {
  unsigned long flags;
  __asm__ volatile("mrs %0, daif" : "=r"(flags));
  return flags & (1UL << 7);
}

#endif
