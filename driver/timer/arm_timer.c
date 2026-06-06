#include <types.h>
#include <driver/arm_timer.h>

static u32 timer_freq = 0;

void arm_timer_init(u32 tick_hz) {
    // Read frequency
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(timer_freq));
    
    // Set timer value for first tick
    u32 ticks = timer_freq / tick_hz;
    __asm__ volatile("msr cntp_tval_el0, %0" :: "r"(ticks));
    
    // Enable timer, unmask interrupt
    u32 ctrl = 1; // ENABLE=1, IMASK=0
    __asm__ volatile("msr cntp_ctl_el0, %0" :: "r"(ctrl));
}

void arm_timer_reset(u32 tick_hz) {
    u32 ticks = timer_freq / tick_hz;
    __asm__ volatile("msr cntp_tval_el0, %0" :: "r"(ticks));
}
