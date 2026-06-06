#ifndef __ARM_TIMER_H__
#define __ARM_TIMER_H__

#include <types.h>

void arm_timer_init(u32 tick_hz);
void arm_timer_reset(u32 tick_hz);

#endif // __ARM_TIMER_H__
