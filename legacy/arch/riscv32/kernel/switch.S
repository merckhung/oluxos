.global cpu_switch_to
cpu_switch_to:
    // a0 points to current CpuContext (32-bit)
    // a1 points to next CpuContext (32-bit)

    // Save current callee-saved registers (4-byte alignment/size)
    sw ra, 0(a0)
    sw sp, 4(a0)
    sw s0, 8(a0)
    sw s1, 12(a0)
    sw s2, 16(a0)
    sw s3, 20(a0)
    sw s4, 24(a0)
    sw s5, 28(a0)
    sw s6, 32(a0)
    sw s7, 36(a0)
    sw s8, 40(a0)
    sw s9, 44(a0)
    sw s10, 48(a0)
    sw s11, 52(a0)

    // Restore next callee-saved registers
    lw ra, 0(a1)
    lw sp, 4(a1)
    lw s0, 8(a1)
    lw s1, 12(a1)
    lw s2, 16(a1)
    lw s3, 20(a1)
    lw s4, 24(a1)
    lw s5, 28(a1)
    lw s6, 32(a1)
    lw s7, 36(a1)
    lw s8, 40(a1)
    lw s9, 44(a1)
    lw s10, 48(a1)
    lw s11, 52(a1)

    ret

.global thread_entry_wrapper
thread_entry_wrapper:
    // s1 contains entry function
    jalr s1
    // if it returns, call thread_exit
    call thread_exit

.global userspace_entry_wrapper
userspace_entry_wrapper:
    // s1 = user entry point
    // s2 = user stack pointer
    // s3 = kernel stack top

    // Write user entry point to sepc
    csrw sepc, s1

    // Setup kernel stack in sscratch for traps
    csrw sscratch, s3

    // Configure sstatus:
    // Clear SPP (bit 8) to return to User Mode (0)
    // Set SPIE (bit 5) to enable interrupts after sret
    csrr t0, sstatus
    li t1, ~(1 << 8)
    and t0, t0, t1
    ori t0, t0, (1 << 5)
    csrw sstatus, t0

    // Set user stack pointer
    mv sp, s2

    // Clear registers to avoid leaking kernel data
    li ra, 0
    li gp, 0
    li tp, 0
    li t0, 0
    li t1, 0
    li t2, 0
    li s0, 0
    li s1, 0
    li s2, 0
    li s3, 0
    li s4, 0
    li s5, 0
    li s6, 0
    li s7, 0
    li s8, 0
    li s9, 0
    li s10, 0
    li s11, 0
    li a0, 0
    li a1, 0
    li a2, 0
    li a3, 0
    li a4, 0
    li a5, 0
    li a6, 0
    li a7, 0

    sret
