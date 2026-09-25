#include <types.h>

extern void trap_entry(void);

void trap_init(void) {
  // Write address of trap_entry to stvec.
  // Lower 2 bits are 00 (Direct mode: all traps jump to base address).
  __asm__ volatile("csrw stvec, %0" :: "r"(trap_entry));
}

void IntDisable(void) {
  // Clear sstatus.SIE (bit 1) to disable interrupts in S-mode
  __asm__ volatile("csrc sstatus, 2" ::: "memory");
}

void IntEnable(void) {
  // Set sstatus.SIE (bit 1) to enable interrupts in S-mode
  __asm__ volatile("csrs sstatus, 2" ::: "memory");
}
