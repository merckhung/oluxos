.global trap_entry
.extern trap_handler
.extern cpus
.align 4

trap_entry:
    // Swap sp and sscratch
    // If we came from user: sp = CpuLocal*, sscratch = user_sp
    // If we came from kernel: sp = 0, sscratch = kernel_sp
    csrrw sp, sscratch, sp
    bnez sp, .Lfrom_usermode

    // Trap from kernel:
    // Restore original sp from sscratch
    csrrw sp, sscratch, sp
    
    // Allocate space on kernel stack
    addi sp, sp, -288
    
    // Save registers (standard S-mode save)
    sd x1, 8(sp)    // ra
    addi x1, sp, 288
    sd x1, 16(sp)   // sp
    sd x3, 24(sp)   // gp
    sd x4, 32(sp)   // tp (kernel tp)
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
    // sp = CpuLocal*, sscratch = user_sp
    
    // Save t0, t1, t2, t3 in CpuLocal temp area (offset 32)
    sd t0, 32(sp)
    sd t1, 40(sp)
    sd t2, 48(sp)
    sd t3, 56(sp)
    
    // Save user_sp in CpuLocal.user_sp
    csrr t0, sscratch
    sd t0, 0(sp)
    
    // Load kernel_stack (stack top) from CpuLocal
    ld t1, 8(sp)
    
    // Load hartid from CpuLocal into t2 (temp)
    ld t2, 16(sp)
    
    // Clear sscratch for kernel
    li t3, 0
    csrw sscratch, t3
    
    // Keep CpuLocal* in t3 (temp) before switching sp
    mv t3, sp
    
    // Switch to kernel stack
    mv sp, t1
    addi sp, sp, -288
    
    // Save user registers to stack frame
    sd x1, 8(sp)    // ra
    // user sp was saved in CpuLocal.user_sp, which we copied to t0
    sd t0, 16(sp)   // user sp
    sd x3, 24(sp)   // gp
    sd x4, 32(sp)   // user tp
    
    // Restore kernel tp (hartid)
    mv tp, t2
    
    // Save user t0, t1 from CpuLocal (t3) temp area
    ld t0, 32(t3)
    ld t1, 40(t3)
    sd t0, 40(sp)   // user t0
    sd t1, 48(sp)   // user t1
    
    // Save user t2, t3 from CpuLocal (t3) temp area
    ld t0, 48(t3)
    ld t1, 56(t3)
    sd t0, 56(sp)   // user t2
    sd t1, 224(sp)  // user t3 (x28)
    
    // Save remaining registers
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
    
    // Call C trap handler
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
    andi t0, t0, (1 << 8)
    bnez t0, .Lret_to_kernel
    
    // Returning to User Mode:
    // Calculate CpuLocal* in t0: t0 = &cpus[tp]
    la t0, cpus
    li t1, 72 // sizeof(CpuLocal) = 72
    mul t1, tp, t1
    add t0, t0, t1
    
    // Set sscratch to CpuLocal*
    csrw sscratch, t0
    
    // Restore user registers
    ld x1, 8(sp)    // ra
    ld x3, 24(sp)   // gp
    ld x4, 32(sp)   // user tp (overwrites kernel tp!)
    ld x5, 40(sp)   // user t0
    ld x6, 48(sp)   // user t1
    ld x7, 56(sp)   // user t2
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
    ld x28, 224(sp) // user t3
    ld x29, 232(sp) // t4
    ld x30, 240(sp) // t5
    ld x31, 248(sp) // t6
    
    // Restore user sp
    ld sp, 16(sp)
    sret

.Lret_to_kernel:
    // Returning to Kernel Mode:
    ld x1, 8(sp)    // ra
    ld x3, 24(sp)   // gp
    ld x4, 32(sp)   // tp (kernel tp)
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
    
    addi sp, sp, 288
    sret
