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

.Ljump_kernel:
    // Jump to krn_entry
    call krn_entry

.Lhang:
1:  wfi
    j 1b


.section ".data"
.align 12
.global boot_pg_dir

boot_pg_dir:
    // Entry 0: maps 0-1GB physical (0x00000000 - 0x3FFFFFFF) -> Peripherals
    .quad 0xC7
    // Entry 1: empty
    .quad 0
    // Entry 2: maps 2-3GB physical (0x80000000 - 0xBFFFFFFF) -> RAM
    .quad 0x200000CF
    // Remaining 509 entries are 0
    .zero (512 - 3) * 8
