.section ".text.boot"
.global _start

_start:
    // a0 = hartid, a1 = dtb
    // Only hart 0 boots for now, others hang
    bnez a0, .Lhang

    // Disable interrupts (Supervisor Interrupt Enable = 0)
    csrw sie, zero

    // Set stack pointer
    la sp, _stack_top

    // Clear BSS
    la t0, _bss_start
    la t1, _bss_end
.Lclear_bss:
    bgeu t0, t1, .Ljump_kernel
    sd zero, 0(t0)
    addi t0, t0, 8
    j .Lclear_bss

.Ljump_kernel:
    // Jump to krn_entry
    call krn_entry

.Lhang:
1:  wfi
    j 1b
