/*
 * Generic interrupt dispatch on top of the root interrupt controller.
 */
#include <asm/ptrace.h>
#include <olux/irq.h>
#include <olux/kernel.h>
#include <olux/sched.h>
#include <olux/smp.h>

struct irq_desc {
  irq_handler_t handler;
  void *arg;
  const char *name;
  u64 count;
};

static struct irq_desc irq_descs[NR_IRQS];
static const struct irq_chip *chip;
static DEFINE_SPINLOCK(irq_lock);

void irq_set_chip(const struct irq_chip *c) { chip = c; }
const struct irq_chip *irq_get_chip(void) { return chip; }

int request_irq(int irq, irq_handler_t h, void *arg, const char *name) {
  if (irq < 0 || irq >= NR_IRQS || !chip) return -EINVAL;
  unsigned long f = spin_lock_irqsave(&irq_lock);
  if (irq_descs[irq].handler) {
    spin_unlock_irqrestore(&irq_lock, f);
    return -EBUSY;
  }
  irq_descs[irq].handler = h;
  irq_descs[irq].arg = arg;
  irq_descs[irq].name = name;
  spin_unlock_irqrestore(&irq_lock, f);
  chip->enable(irq);
  return 0;
}

void free_irq(int irq) {
  if (irq < 0 || irq >= NR_IRQS) return;
  chip->disable(irq);
  unsigned long f = spin_lock_irqsave(&irq_lock);
  irq_descs[irq].handler = NULL;
  spin_unlock_irqrestore(&irq_lock, f);
}

void enable_irq(int irq) { chip->enable(irq); }
void disable_irq(int irq) { chip->disable(irq); }
u64 irq_count(int irq) { return irq < NR_IRQS ? irq_descs[irq].count : 0; }
const char *irq_name(int irq) { return irq < NR_IRQS ? irq_descs[irq].name : NULL; }

void smp_handle_sgi(int sgi);
void irq_handler(struct pt_regs *regs);

void irq_handler(struct pt_regs *regs) {
  struct cpu *c = this_cpu();
  c->irq_depth++;
  for (;;) {
    int irq = chip->ack();
    if (irq < 0) break;
    c->irq_count++;
    if (irq < 16) {
      smp_handle_sgi(irq);
      chip->eoi(irq);
      continue;
    }
    struct irq_desc *d = &irq_descs[irq];
    d->count++;
    if (d->handler) d->handler(irq, d->arg);
    else pr_warn("irq: spurious interrupt %d\n", irq);
    chip->eoi(irq);
  }
  c->irq_depth--;
}
