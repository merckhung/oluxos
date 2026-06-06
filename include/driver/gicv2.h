#ifndef __GICV2_H__
#define __GICV2_H__

#include <types.h>

void gicv2_init(void);
void gicv2_enable_irq(u32 irq);
void gicv2_disable_irq(u32 irq);
void gicv2_set_irq_priority(u32 irq, u8 priority);
u32 gicv2_acknowledge_irq(void);
void gicv2_end_of_irq(u32 irq);

#endif // __GICV2_H__
