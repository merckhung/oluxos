.section ".text.boot"
.global _start
.extern krn_entry
.extern krn_entry_secondary

_start:
    // a0 = hartid, a1 = dtb
    mv tp, a0 // Save hartid in tp
    
    // Atomically check and set cold_boot_done
    la t0, cold_boot_done
    li t1, 1
    amoswap.d.aq t2, t1, (t0) // t2 = old value, set to 1
    bnez t2, .Lsecondary_boot  // If old value was 1, we are secondary

    // Boot hart:
    // Disable interrupts (Supervisor Interrupt Enable = 0)
    csrw sie, zero
    
    // Clear sscratch to ensure early S-mode traps are recognized correctly
    csrw sscratch, zero

    // Set stack pointer for Boot Hart (using its hartid)
    la sp, _stack_top
    li t0, 0x4000 // 16KB
    mul t0, a0, t0
    sub sp, sp, t0

    // Clear BSS
    la t0, _bss_start
    la t1, _bss_end
.Lclear_bss:
    bgeu t0, t1, .Lsetup_mmu
    sd zero, 0(t0)
    addi t0, t0, 8
    j .Lclear_bss

.Lsetup_mmu:
    // Setup boot page table (Sv39)
    la t0, boot_pg_dir
    srli t0, t0, 12 // PPN of boot_pg_dir
    li t1, 8        // Mode Sv39 (MODE = 8)
    slli t1, t1, 60
    or t0, t0, t1   // Combine Mode and PPN
    csrw satp, t0   // Write satp
    sfence.vma      // Flush TLB

    // Set boot_done = 1
    la t0, boot_done
    li t1, 1
    sd t1, 0(t0)

.Ljump_kernel:
    // Jump to krn_entry, passing hartid in a0
    call krn_entry

.Lsecondary_boot:
    // Secondary harts:
    // Spin on boot_done first to ensure boot hart has finished BSS clearing and MMU setup
    la t0, boot_done
1:  ld t1, 0(t0)
    beqz t1, 1b

    // Disable interrupts
    csrw sie, zero
    csrw sscratch, zero

    // Set up stack for this CPU: sp = _stack_top - hartid * 16KB
    la sp, _stack_top
    li t0, 0x4000 // 16KB
    mul t0, a0, t0
    sub sp, sp, t0

    // Enable MMU (same as Boot Hart)
    la t0, boot_pg_dir
    srli t0, t0, 12
    li t1, 8
    slli t1, t1, 60
    or t0, t0, t1
    csrw satp, t0
    sfence.vma

    // Call secondary entry (a0 already contains hartid)
    call krn_entry_secondary

.Lhang:
1:  wfi
    j 1b


.section ".data"
.align 12
.global boot_pg_dir
.global boot_done
.global cold_boot_done

cold_boot_done:
    .quad 0

boot_done:
    .quad 0

.align 12
boot_pg_dir:
    // Entry 0: maps 0-1GB physical (0x00000000 - 0x3FFFFFFF) -> Peripherals
    .quad 0xC7
    // Entry 1: empty
    .quad 0
    // Entry 2: maps 2-3GB physical (0x80000000 - 0xBFFFFFFF) -> RAM
    .quad 0x200000CF
    // Remaining 509 entries are 0
    .zero (512 - 3) * 8
