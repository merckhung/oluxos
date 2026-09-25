.global trap_entry
.align 4
trap_entry:
    // Swap sp and sscratch to get kernel stack if coming from U-mode
    csrrw sp, sscratch, sp
    bnez sp, .Lfrom_usermode
    
    // If sp is 0, we were already in kernel mode, so restore original sp from sscratch
    csrrw sp, sscratch, sp
    
    // Allocate space for RISCV32Registers on kernel stack (144 bytes)
    addi sp, sp, -144
    
    // Save general purpose registers (except sp)
    sw x1, 4(sp)    // ra
    // Save the original sp! The original sp is the current sp + 144.
    addi x1, sp, 144
    sw x1, 8(sp)    // sp
    sw x3, 12(sp)   // gp
    sw x4, 16(sp)   // tp
    sw x5, 20(sp)   // t0
    sw x6, 24(sp)   // t1
    sw x7, 28(sp)   // t2
    sw x8, 32(sp)   // s0/fp
    sw x9, 36(sp)   // s1
    sw x10, 40(sp)  // a0
    sw x11, 44(sp)  // a1
    sw x12, 48(sp)  // a2
    sw x13, 52(sp)  // a3
    sw x14, 56(sp)  // a4
    sw x15, 60(sp)  // a5
    sw x16, 64(sp)  // a6
    sw x17, 68(sp)  // a7
    sw x18, 72(sp)  // s2
    sw x19, 76(sp)  // s3
    sw x20, 80(sp)  // s4
    sw x21, 84(sp)  // s5
    sw x22, 88(sp)  // s6
    sw x23, 92(sp)  // s7
    sw x24, 96(sp)  // s8
    sw x25, 100(sp) // s9
    sw x26, 104(sp) // s10
    sw x27, 108(sp) // s11
    sw x28, 112(sp) // t3
    sw x29, 116(sp) // t4
    sw x30, 120(sp) // t5
    sw x31, 124(sp) // t6
    
    j .Lsave_csrs

.Lfrom_usermode:
    // sp now contains the kernel stack pointer
    // sscratch contains the user stack pointer
    
    // Allocate space on kernel stack
    addi sp, sp, -144
    
    // Save general purpose registers
    sw x1, 4(sp)    // ra
    // Save user sp from sscratch
    csrr x1, sscratch
    sw x1, 8(sp)    // user sp
    li x1, 0
    csrw sscratch, x1 // Clear sscratch for S-mode execution
    
    sw x3, 12(sp)   // gp
    sw x4, 16(sp)   // tp
    sw x5, 20(sp)   // t0
    sw x6, 24(sp)   // t1
    sw x7, 28(sp)   // t2
    sw x8, 32(sp)   // s0/fp
    sw x9, 36(sp)   // s1
    sw x10, 40(sp)  // a0
    sw x11, 44(sp)  // a1
    sw x12, 48(sp)  // a2
    sw x13, 52(sp)  // a3
    sw x14, 56(sp)  // a4
    sw x15, 60(sp)  // a5
    sw x16, 64(sp)  // a6
    sw x17, 68(sp)  // a7
    sw x18, 72(sp)  // s2
    sw x19, 76(sp)  // s3
    sw x20, 80(sp)  // s4
    sw x21, 84(sp)  // s5
    sw x22, 88(sp)  // s6
    sw x23, 92(sp)  // s7
    sw x24, 96(sp)  // s8
    sw x25, 100(sp) // s9
    sw x26, 104(sp) // s10
    sw x27, 108(sp) // s11
    sw x28, 112(sp) // t3
    sw x29, 116(sp) // t4
    sw x30, 120(sp) // t5
    sw x31, 124(sp) // t6

.Lsave_csrs:
    // Save control registers
    csrr t0, sepc
    sw t0, 128(sp)
    
    csrr t0, sstatus
    sw t0, 132(sp)
    
    csrr t0, stval
    sw t0, 136(sp)
    
    csrr t0, scause
    sw t0, 140(sp)
    
    // Call C trap handler: void trap_handler(RISCV32Registers* regs);
    mv a0, sp
    call trap_handler

.global trap_return
trap_return:
    // Restore control registers
    lw t0, 128(sp)
    csrw sepc, t0
    
    lw t0, 132(sp)
    csrw sstatus, t0
    
    // Check if we are returning to user mode (sstatus.SPP == 0)
    // SPP is bit 8.
    andi t0, t0, (1 << 8)
    bnez t0, .Lret_to_kernel
    
    // Returning to User Mode!
    // 1. Set sscratch to kernel stack top (current sp + 144)
    addi t0, sp, 144
    csrw sscratch, t0
    
    // 2. Restore all general registers (except sp)
    lw x1, 4(sp)    // ra
    lw x3, 12(sp)   // gp
    lw x4, 16(sp)   // tp
    lw x5, 20(sp)   // t0
    lw x6, 24(sp)   // t1
    lw x7, 28(sp)   // t2
    lw x8, 32(sp)   // s0/fp
    lw x9, 36(sp)   // s1
    lw x10, 40(sp)  // a0
    lw x11, 44(sp)  // a1
    lw x12, 48(sp)  // a2
    lw x13, 52(sp)  // a3
    lw x14, 56(sp)  // a4
    lw x15, 60(sp)  // a5
    lw x16, 64(sp)  // a6
    lw x17, 68(sp)  // a7
    lw x18, 72(sp)  // s2
    lw x19, 76(sp)  // s3
    lw x20, 80(sp)  // s4
    lw x21, 84(sp)  // s5
    lw x22, 88(sp)  // s6
    lw x23, 92(sp)  // s7
    lw x24, 96(sp)  // s8
    lw x25, 100(sp) // s9
    lw x26, 104(sp) // s10
    lw x27, 108(sp) // s11
    lw x28, 112(sp) // t3
    lw x29, 116(sp) // t4
    lw x30, 120(sp) // t5
    lw x31, 124(sp) // t6
    
    // 3. Load user sp directly from stack frame
    lw sp, 8(sp)
    sret

.Lret_to_kernel:
    // Returning to Kernel Mode!
    lw x1, 4(sp)    // ra
    lw x3, 12(sp)   // gp
    lw x4, 16(sp)   // tp
    lw x5, 20(sp)   // t0
    lw x6, 24(sp)   // t1
    lw x7, 28(sp)   // t2
    lw x8, 32(sp)   // s0/fp
    lw x9, 36(sp)   // s1
    lw x10, 40(sp)  // a0
    lw x11, 44(sp)  // a1
    lw x12, 48(sp)  // a2
    lw x13, 52(sp)  // a3
    lw x14, 56(sp)  // a4
    lw x15, 60(sp)  // a5
    lw x16, 64(sp)  // a6
    lw x17, 68(sp)  // a7
    lw x18, 72(sp)  // s2
    lw x19, 76(sp)  // s3
    lw x20, 80(sp)  // s4
    lw x21, 84(sp)  // s5
    lw x22, 88(sp)  // s6
    lw x23, 92(sp)  // s7
    lw x24, 96(sp)  // s8
    lw x25, 100(sp) // s9
    lw x26, 104(sp) // s10
    lw x27, 108(sp) // s11
    lw x28, 112(sp) // t3
    lw x29, 116(sp) // t4
    lw x30, 120(sp) // t5
    lw x31, 124(sp) // t6
    
    // Restore kernel sp
    addi sp, sp, 144
    sret
