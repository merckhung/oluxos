.global trap_entry
.align 4
trap_entry:
    // Swap sp and sscratch to get kernel stack if coming from U-mode
    csrrw sp, sscratch, sp
    bnez sp, .Lfrom_usermode
    
    // If sp is 0, we were already in kernel mode, so restore original sp from sscratch
    csrrw sp, sscratch, sp
    
    // Allocate space for RISCV64Registers on kernel stack
    addi sp, sp, -288
    
    // Save general purpose registers (except sp)
    sd x1, 8(sp)    // ra
    // Save the original sp! The original sp is the current sp + 288.
    addi x1, sp, 288
    sd x1, 16(sp)   // sp
    sd x3, 24(sp)   // gp
    sd x4, 32(sp)   // tp
    sd x5, 40(sp)   // t0
    sd x6, 48(sp)   // t1
    sd x7, 56(sp)   // t2
    sd x8, 64(sp)   // s0/fp
    sd x9, 72(sp)   // s1
    sd x10, 80(sp)  // a0
    sd x11, 88(sp)  // a1
    sd x12, 96(sp)  // a2
    sd x13, 104(sp) // a3
    sd x14, 112(sp) // a4
    sd x15, 120(sp) // a5
    sd x16, 128(sp) // a6
    sd x17, 136(sp) // a7
    sd x18, 144(sp) // s2
    sd x19, 152(sp) // s3
    sd x20, 160(sp) // s4
    sd x21, 168(sp) // s5
    sd x22, 176(sp) // s6
    sd x23, 184(sp) // s7
    sd x24, 192(sp) // s8
    sd x25, 200(sp) // s9
    sd x26, 208(sp) // s10
    sd x27, 216(sp) // s11
    sd x28, 224(sp) // t3
    sd x29, 232(sp) // t4
    sd x30, 240(sp) // t5
    sd x31, 248(sp) // t6
    
    j .Lsave_csrs

.Lfrom_usermode:
    // sp now contains the kernel stack pointer (which is kernel stack top for this thread)
    // sscratch contains the user stack pointer
    
    // Allocate space on kernel stack
    addi sp, sp, -288
    
    // Save general purpose registers
    sd x1, 8(sp)    // ra
    // Save user sp from sscratch
    csrr x1, sscratch
    sd x1, 16(sp)   // user sp
    
    sd x3, 24(sp)   // gp
    sd x4, 32(sp)   // tp
    sd x5, 40(sp)   // t0
    sd x6, 48(sp)   // t1
    sd x7, 56(sp)   // t2
    sd x8, 64(sp)   // s0/fp
    sd x9, 72(sp)   // s1
    sd x10, 80(sp)  // a0
    sd x11, 88(sp)  // a1
    sd x12, 96(sp)  // a2
    sd x13, 104(sp) // a3
    sd x14, 112(sp) // a4
    sd x15, 120(sp) // a5
    sd x16, 128(sp) // a6
    sd x17, 136(sp) // a7
    sd x18, 144(sp) // s2
    sd x19, 152(sp) // s3
    sd x20, 160(sp) // s4
    sd x21, 168(sp) // s5
    sd x22, 176(sp) // s6
    sd x23, 184(sp) // s7
    sd x24, 192(sp) // s8
    sd x25, 200(sp) // s9
    sd x26, 208(sp) // s10
    sd x27, 216(sp) // s11
    sd x28, 224(sp) // t3
    sd x29, 232(sp) // t4
    sd x30, 240(sp) // t5
    sd x31, 248(sp) // t6

.Lsave_csrs:
    // Save control registers
    csrr t0, sepc
    sd t0, 256(sp)
    
    csrr t0, sstatus
    sd t0, 264(sp)
    
    csrr t0, stval
    sd t0, 272(sp)
    
    csrr t0, scause
    sd t0, 280(sp)
    
    // Call C trap handler: void trap_handler(RISCV64Registers* regs);
    mv a0, sp
    call trap_handler

.global trap_return
trap_return:
    // Restore control registers
    ld t0, 256(sp)
    csrw sepc, t0
    
    ld t0, 264(sp)
    csrw sstatus, t0
    
    // Check if we are returning to user mode (sstatus.SPP == 0)
    // SPP is bit 8.
    andi t0, t0, (1 << 8)
    bnez t0, .Lret_to_kernel
    
    // Returning to User Mode!
    // Restore sscratch to hold the kernel stack top: current sp + 288
    addi t0, sp, 288
    csrw sscratch, t0
    
    // Restore user sp to sscratch temporarily
    ld t0, 16(sp)
    csrw sscratch, t0
    
    // Restore general purpose registers (except sp)
    ld x1, 8(sp)    // ra
    ld x3, 24(sp)   // gp
    ld x4, 32(sp)   // tp
    ld x5, 40(sp)   // t0
    ld x6, 48(sp)   // t1
    ld x7, 56(sp)   // t2
    ld x8, 64(sp)   // s0/fp
    ld x9, 72(sp)   // s1
    ld x10, 80(sp)  // a0
    ld x11, 88(sp)  // a1
    ld x12, 96(sp)  // a2
    ld x13, 104(sp) // a3
    ld x14, 112(sp) // a4
    ld x15, 120(sp) // a5
    ld x16, 128(sp) // a6
    ld x17, 136(sp) // a7
    ld x18, 144(sp) // s2
    ld x19, 152(sp) // s3
    ld x20, 160(sp) // s4
    ld x21, 168(sp) // s5
    ld x22, 176(sp) // s6
    ld x23, 184(sp) // s7
    ld x24, 192(sp) // s8
    ld x25, 200(sp) // s9
    ld x26, 208(sp) // s10
    ld x27, 216(sp) // s11
    ld x28, 224(sp) // t3
    ld x29, 232(sp) // t4
    ld x30, 240(sp) // t5
    ld x31, 248(sp) // t6
    
    // Swap sp and sscratch: sp gets user sp, sscratch gets kernel stack top
    csrrw sp, sscratch, sp
    sret

.Lret_to_kernel:
    // Returning to Kernel Mode!
    ld x1, 8(sp)    // ra
    ld x3, 24(sp)   // gp
    ld x4, 32(sp)   // tp
    ld x5, 40(sp)   // t0
    ld x6, 48(sp)   // t1
    ld x7, 56(sp)   // t2
    ld x8, 64(sp)   // s0/fp
    ld x9, 72(sp)   // s1
    ld x10, 80(sp)  // a0
    ld x11, 88(sp)  // a1
    ld x12, 96(sp)  // a2
    ld x13, 104(sp) // a3
    ld x14, 112(sp) // a4
    ld x15, 120(sp) // a5
    ld x16, 128(sp) // a6
    ld x17, 136(sp) // a7
    ld x18, 144(sp) // s2
    ld x19, 152(sp) // s3
    ld x20, 160(sp) // s4
    ld x21, 168(sp) // s5
    ld x22, 176(sp) // s6
    ld x23, 184(sp) // s7
    ld x24, 192(sp) // s8
    ld x25, 200(sp) // s9
    ld x26, 208(sp) // s10
    ld x27, 216(sp) // s11
    ld x28, 224(sp) // t3
    ld x29, 232(sp) // t4
    ld x30, 240(sp) // t5
    ld x31, 248(sp) // t6
    
    // Restore kernel sp
    addi sp, sp, 288
    sret
