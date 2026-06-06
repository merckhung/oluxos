#include <types.h>
#include <driver/arm_timer.h>

static uint32_t timer_freq = 0;

void arm_timer_init(uint32_t tick_hz) {
    // Read frequency
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(timer_freq));
    
    // Set timer value for first tick
    uint32_t ticks = timer_freq / tick_hz;
    __asm__ volatile("msr cntp_tval_el0, %0" :: "r"(ticks));
    
    // Enable timer, unmask interrupt
    uint32_t ctrl = 1; // ENABLE=1, IMASK=0
    __asm__ volatile("msr cntp_ctl_el0, %0" :: "r"(ctrl));
}

void arm_timer_reset(uint32_t tick_hz) {
    uint32_t ticks = timer_freq / tick_hz;
    __asm__ volatile("msr cntp_tval_el0, %0" :: "r"(ticks));
}
