#include <arm64/interrupt.h>
#include <types.h>

void IntDisable(void) { __asm__ volatile("msr daifset, #2" ::: "memory"); }

void IntEnable(void) { __asm__ volatile("msr daifclr, #2" ::: "memory"); }
