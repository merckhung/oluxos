.section ".text.boot"
.global _start

_start:
    // a0 = hartid, a1 = dtb
    // Only hart 0 boots for now, others hang
    bnez a0, .Lhang

    // Disable interrupts (Supervisor Interrupt Enable = 0)
    csrw sie, zero
    
    // Clear sscratch to ensure early S-mode traps are recognized correctly
    csrw sscratch, zero

    // Set stack pointer
    la sp, _stack_top

    // Clear BSS (32-bit version)
    la t0, _bss_start
    la t1, _bss_end
.Lclear_bss:
    bgeu t0, t1, .Lsetup_mmu
    sw zero, 0(t0)
    addi t0, t0, 4
    j .Lclear_bss

.Lsetup_mmu:
    // Setup boot page table (Sv32)
    la t0, boot_pg_dir
    srli t0, t0, 12 // PPN of boot_pg_dir
    li t1, 1        // Mode Sv32 (MODE = 1)
    slli t1, t1, 31
    or t0, t0, t1   // Combine Mode and PPN
    csrw satp, t0   // Write satp
    sfence.vma      // Flush TLB

.Ljump_kernel:
    // Jump to krn_entry
    call krn_entry

.Lhang:
1:  wfi
    j 1b


.section ".data"
.align 12
.global boot_pg_dir

// Sv32 page table: 1024 entries, each 4 bytes (4KB page)
boot_pg_dir:
    // Entry 0 to 63: empty (0-256MB virtual)
    .zero 64 * 4

    // Entry 64: maps 0x10000000 physical -> 0x10000000 virtual (UART)
    // PTE: 0x10000000 >> 2 | 0xC7 = 0x040000C7
    .word 0x040000C7

    // Entry 65 to 511: empty (260MB-2048MB virtual)
    .zero (512 - 65) * 4

    // Entry 512: maps 0x80000000 physical -> 0x80000000 virtual (RAM block 0)
    // PTE: 0x80000000 >> 2 | 0xCF = 0x200000CF
    .word 0x200000CF

    // Entry 513: maps 0x80400000 physical -> 0x80400000 virtual (RAM block 1)
    // PTE: 0x80400000 >> 2 | 0xCF = 0x201000CF
    .word 0x201000CF

    // Entry 514: maps 0x80800000 physical -> 0x80800000 virtual (RAM block 2)
    // PTE: 0x80800000 >> 2 | 0xCF = 0x202000CF
    .word 0x202000CF

    // Entry 515: maps 0x80C00000 physical -> 0x80C00000 virtual (RAM block 3)
    // PTE: 0x80C00000 >> 2 | 0xCF = 0x203000CF
    .word 0x203000CF

    // Remaining 508 entries are 0 (2064MB-4096MB virtual)
    .zero (1024 - 516) * 4
