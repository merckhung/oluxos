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
    // Populate boot_pg_dir entries 512 to 767 (1GB RAM identity map)
    // Entry i: maps (i << 22) physical to (i << 22) virtual.
    // PTE = (i << 20) | 0xCF
    la t0, boot_pg_dir
    li t1, 512          // start index
    li t2, 768          // end index
.Lloop_map:
    bge t1, t2, .Ldone_map
    slli t3, t1, 20
    ori t3, t3, 0xCF
    slli t4, t1, 2
    add t4, t0, t4
    sw t3, 0(t4)
    addi t1, t1, 1
    j .Lloop_map
.Ldone_map:

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
    // Entry 0 to 63: empty
    .zero 64 * 4

    // Entry 64: maps 0x10000000 physical -> 0x10000000 virtual (UART)
    .word 0x040000C7

    // Entry 65 to 1023: empty (will be populated for RAM at boot)
    .zero (1024 - 65) * 4
