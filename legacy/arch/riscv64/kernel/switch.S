.global cpu_switch_to
.extern sched_lock
.extern spin_unlock
.extern IntEnable

cpu_switch_to:
    // a0 points to current CpuContext
    // a1 points to next CpuContext

    // Save current callee-saved registers
    sd ra, 0(a0)
    sd sp, 8(a0)
    sd s0, 16(a0)
    sd s1, 24(a0)
    sd s2, 32(a0)
    sd s3, 40(a0)
    sd s4, 48(a0)
    sd s5, 56(a0)
    sd s6, 64(a0)
    sd s7, 72(a0)
    sd s8, 80(a0)
    sd s9, 88(a0)
    sd s10, 96(a0)
    sd s11, 104(a0)

    // Restore next callee-saved registers
    ld ra, 0(a1)
    ld sp, 8(a1)
    ld s0, 16(a1)
    ld s1, 24(a1)
    ld s2, 32(a1)
    ld s3, 40(a1)
    ld s4, 48(a1)
    ld s5, 56(a1)
    ld s6, 64(a1)
    ld s7, 72(a1)
    ld s8, 80(a1)
    ld s9, 88(a1)
    ld s10, 96(a1)
    ld s11, 104(a1)

    ret

.global thread_entry_wrapper
thread_entry_wrapper:
    // Release scheduler lock before starting thread
    la a0, sched_lock
    call spin_unlock
    
    // Enable interrupts for kernel thread
    call IntEnable

    // s1 contains entry function
    jalr s1
    // if it returns, call thread_exit
    call thread_exit

.extern cpus

.global userspace_entry_wrapper
userspace_entry_wrapper:
    // s1 = user entry point
    // s2 = user stack pointer
    // s3 = kernel stack top (no longer used for sscratch directly)

    // Release scheduler lock before entering userspace
    la a0, sched_lock
    call spin_unlock

    // Write user entry point to sepc
    csrw sepc, s1

    // Setup CpuLocal* in sscratch for traps (sscratch = &cpus[tp])
    la t0, cpus
    li t1, 72 // sizeof(CpuLocal) = 72
    mul t1, tp, t1
    add t0, t0, t1
    csrw sscratch, t0

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
