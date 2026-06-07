.section ".text.boot"
.global _start

_start:
    // Check CPU ID, only Aff0 (CPU ID) == 0 is primary
    mrs     x0, mpidr_el1
    and     x0, x0, #0xff
    cbz     x0, .Lprimary_cpu
.Lsecondary_hang:
    wfe
    b       .Lsecondary_hang

.Lprimary_cpu:
    // Check current EL
    mrs     x0, CurrentEL
    and     x0, x0, #0xc
    cmp     x0, #0x4 // EL1
    b.eq    .Lsetup_el1
    cmp     x0, #0x8 // EL2
    b.eq    .Ldrop_to_el1
    cmp     x0, #0xc // EL3
    b.eq    .Ldrop_to_el2
    b       .Lhang

.Ldrop_to_el2:
    // Enable AArch64 in EL2 (from EL3)
    mov     x0, #0x501              // RW=1 (AArch64 next EL), HCE=1, NS=1
    msr     scr_el3, x0

    // Setup SPSR_EL3 for EL2h
    mov     x0, #0x3c9              // EL2h with interrupts masked (D, A, I, F)
    msr     spsr_el3, x0

    // Setup return address to EL2 entry
    adr     x0, .Ldrop_to_el1
    msr     elr_el3, x0

    eret

.Ldrop_to_el1:
    // Enable AArch64 in EL1
    mov     x0, #(1 << 31) // RW=1 (AArch64)
    msr     hcr_el2, x0

    // Setup SPSR_EL2 for EL1h
    mov     x0, #0x3c5 // EL1h with interrupts masked (D, A, I, F)
    msr     spsr_el2, x0

    // Setup return address to EL1 entry
    adr     x0, .Lsetup_el1
    msr     elr_el2, x0

    eret

.Lsetup_el1:
    // Setup stack pointer
    ldr     x0, =_stack_top
    mov     sp, x0

    // Clear BSS
    ldr     x0, =_bss_start
    ldr     x1, =_bss_end
.Lclear_bss:
    cmp     x0, x1
    b.hs    .Lsetup_mmu
    str     xzr, [x0], #8
    b       .Lclear_bss

.Lsetup_mmu:
    // Setup Vector Table
    ldr     x0, =el1_vector_table
    msr     vbar_el1, x0
    isb



    // Setup MAIR_EL1
    ldr     x0, =0xFF00 // Attr0 = Device, Attr1 = Normal
    msr     mair_el1, x0

    // Setup TCR_EL1
    // T0SZ = 25 (39-bit VA, starts at L1)
    // TG0 = 0 (4KB)
    // SH0 = 3 (Inner Shareable)
    // ORGN0 = 1 (Outer WB WA)
    // IRGN0 = 1 (Inner WB WA)
    // EPD1 = 1 (Disable TTBR1)
    ldr     x0, =((25) | (3 << 12) | (1 << 10) | (1 << 8) | (1 << 23))
    msr     tcr_el1, x0

    // Setup TTBR0_EL1
    adr     x0, pg_dir
    msr     ttbr0_el1, x0
    isb

    // Enable MMU in SCTLR_EL1
    mrs     x0, sctlr_el1
    orr     x0, x0, #1 // M = 1 (Enable MMU)
    orr     x0, x0, #(1 << 12) // I = 1 (Enable Instruction Cache)
    msr     sctlr_el1, x0
    isb

    // Force synchronization
    dsb     sy
    isb

.Ljump_kernel:
    bl      krn_entry

.Lhang:
1:  wfe
    b       1b


.section ".data"
.align 12
#if CONFIG_BOARD_RPI4
.global pg_dir, l2_table_0, l2_table_3, l3_table_0
#else
.global pg_dir, l2_table_0, l3_table_0
#endif

#if CONFIG_BOARD_RPI4

pg_dir:
    // Entry 0: 0-1GB virtual mapped to 0-1GB physical RAM (boot mapping)
    .quad 0x0040000000000705
    // Entry 1: 1-2GB virtual mapped to 1-2GB physical RAM (kernel space, same as virt!)
    .quad 0x0040000040000705
    // Entry 2: empty
    .quad 0
    // Entry 3: points to l2_table_3 (covers 3GB - 4GB peripherals)
    .quad l2_table_3 + 0x3
    .zero 4096 - 32

l2_table_0:
    // Entry 0: points to l3_table_0 (Table descriptor)
    .quad l3_table_0 + 0x3
    // Entry 1 to 511: empty
    .zero 511 * 8

l2_table_3:
    // Static definitions for RPi4 peripherals.
    .zero 496 * 8
    // Entry 496: Mailbox/Peripherals (Device, 2MB block, physical 0xFE000000)
    .quad 0x00600000FE000401
    // Entry 497: UART0 (Device, 2MB block, physical 0xFE200000)
    .quad 0x00600000FE200401
    .zero (508 - 497 - 1) * 8
    // Entry 508: GIC (Device, 2MB block, physical 0xFF800000)
    .quad 0x00600000FF800401
    .zero (512 - 508 - 1) * 8

l3_table_0:
    // Maps 0-2MB in 4KB pages. Empty at boot.
    .zero 4096

#else // QEMU virt (Default)

pg_dir:
    // Entry 0: points to l2_table_0 (Table descriptor)
    .quad l2_table_0 + 0x3
    // Entry 1: 1-2GB Normal (RAM)
    .quad 0x0040000040000705
    .zero 4096 - 16

l2_table_0:
    // Entry 0: points to l3_table_0 (Table descriptor)
    .quad l3_table_0 + 0x3
    // Entry 1 to 63 (63 entries)
    .zero 63 * 8
    // Entry 64: GIC (Device, 2MB block, physical 0x08000000)
    .quad 0x0060000008000401
    // Entry 65 to 71 (7 entries)
    .zero 7 * 8
    // Entry 72: UART (Device, 2MB block, physical 0x09000000)
    .quad 0x0060000009000401
    // Entry 73 to 511 (439 entries)
    .zero 439 * 8

l3_table_0:
    // Maps 0-2MB in 4KB pages. Empty at boot.
    .zero 4096

#endif

