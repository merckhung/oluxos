#ifndef __GICV2_H__
#define __GICV2_H__

#include <types.h>

void gicv2_init(void);
void gicv2_enable_irq(uint32_t irq);
void gicv2_disable_irq(uint32_t irq);
void gicv2_set_irq_priority(uint32_t irq, uint8_t priority);
uint32_t gicv2_acknowledge_irq(void);
void gicv2_end_of_irq(uint32_t irq);

#endif  // __GICV2_H__
