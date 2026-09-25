#ifndef __ARM_TIMER_H__
#define __ARM_TIMER_H__

#include <types.h>

void arm_timer_init(uint32_t tick_hz);
void arm_timer_reset(uint32_t tick_hz);

#endif  // __ARM_TIMER_H__
