.global cpu_switch_to
cpu_switch_to:
    // x0 points to current CpuContext
    // x1 points to next CpuContext

    // Save current callee-saved registers
    mov     x2, sp
    stp     x19, x20, [x0, #0]
    stp     x21, x22, [x0, #16]
    stp     x23, x24, [x0, #32]
    stp     x25, x26, [x0, #48]
    stp     x27, x28, [x0, #64]
    stp     x29, x30, [x0, #80] // fp, lr
    str     x2, [x0, #96]       // sp

    // Restore next callee-saved registers
    ldp     x19, x20, [x1, #0]
    ldp     x21, x22, [x1, #16]
    ldp     x23, x24, [x1, #32]
    ldp     x25, x26, [x1, #48]
    ldp     x27, x28, [x1, #64]
    ldp     x29, x30, [x1, #80] // fp, lr
    ldr     x2, [x1, #96]       // sp
    mov     sp, x2

    ret

.global thread_entry_wrapper
thread_entry_wrapper:
    // x19 contains entry function
    blr     x19
    // if it returns, call thread_exit
    bl      thread_exit

.global userspace_entry_wrapper
userspace_entry_wrapper:
    // Setup SP_EL0 (User Stack)
    msr     sp_el0, x20
    
    // Setup ELR_EL1 (User Entry Point)
    msr     elr_el1, x19
    
    // Setup SPSR_EL1 (EL0t, interrupts enabled)
    mov     x0, #0x0
    msr     spsr_el1, x0
    
    // Clear registers
    mov     x0, xzr
    mov     x1, xzr
    mov     x2, xzr
    mov     x3, xzr
    mov     x4, xzr
    mov     x5, xzr
    mov     x6, xzr
    mov     x7, xzr
    mov     x8, xzr
    mov     x9, xzr
    mov     x10, xzr
    mov     x11, xzr
    mov     x12, xzr
    mov     x13, xzr
    mov     x14, xzr
    mov     x15, xzr
    mov     x16, xzr
    mov     x17, xzr
    mov     x18, xzr
    mov     x19, xzr
    mov     x20, xzr
    mov     x21, xzr
    mov     x22, xzr
    mov     x23, xzr
    mov     x24, xzr
    mov     x25, xzr
    mov     x26, xzr
    mov     x27, xzr
    mov     x28, xzr
    mov     x29, xzr
    mov     x30, xzr
    
    eret
