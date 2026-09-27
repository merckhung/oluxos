#ifndef OLUX_IRQ_H
#define OLUX_IRQ_H

#include <olux/types.h>

#define NR_IRQS 1024
#define IRQ_SGI_RESCHEDULE 0
#define IRQ_SGI_CALL 1
#define IRQ_SGI_STOP 2

#define IRQ_TYPE_EDGE_RISING 1
#define IRQ_TYPE_LEVEL_HIGH 4

typedef void (*irq_handler_t)(int irq, void *arg);

struct irq_chip {
  const char *name;
  void (*enable)(int irq);
  void (*disable)(int irq);
  void (*set_type)(int irq, unsigned type);
  void (*set_affinity)(int irq, int cpu);
  void (*send_sgi)(int sgi, int cpu);
  /* Returns the next pending interrupt (or -1) and acks it. */
  int (*ack)(void);
  void (*eoi)(int irq);
  void (*cpu_init)(void); /* per-CPU interface init (secondaries) */
};

void irq_set_chip(const struct irq_chip *chip);
const struct irq_chip *irq_get_chip(void);
int request_irq(int irq, irq_handler_t h, void *arg, const char *name);
void free_irq(int irq);
void enable_irq(int irq);
void disable_irq(int irq);
u64 irq_count(int irq);
const char *irq_name(int irq);

#endif
