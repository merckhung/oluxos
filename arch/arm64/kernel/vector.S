.align 11 // 2048-byte alignment
.global el1_vector_table
el1_vector_table:
    // === Current EL with SP0 ===
    .align 7
    b   .Lsync_el1_sp0
    .align 7
    b   .Lirq_el1_sp0
    .align 7
    b   .Lfiq_el1_sp0
    .align 7
    b   .Lserror_el1_sp0

    // === Current EL with SPx ===
    .align 7
    b   .Lsync_el1_spx
    .align 7
    b   .Lirq_el1_spx
    .align 7
    b   .Lfiq_el1_spx
    .align 7
    b   .Lserror_el1_spx

    // === Lower EL AArch64 ===
    .align 7
    b   .Lsync_el0_64
    .align 7
    b   .Lirq_el0_64
    .align 7
    b   .Lfiq_el0_64
    .align 7
    b   .Lserror_el0_64

    // === Lower EL AArch32 ===
    .align 7
    b   .Lsync_el0_32
    .align 7
    b   .Lirq_el0_32
    .align 7
    b   .Lfiq_el0_32
    .align 7
    b   .Lserror_el0_32

// Implement minimal handlers
.macro ExceptionStub name, msg
\name:
    mov     x0, x30 // save lr
    adr     x1, 1f
    bl      exception_handler_dump
1:  .asciz  "\msg"
    b       .
.endm

ExceptionStub .Lsync_el1_sp0, "Sync EL1 SP0"
ExceptionStub .Lirq_el1_sp0,  "IRQ EL1 SP0"
ExceptionStub .Lfiq_el1_sp0,  "FIQ EL1 SP0"
ExceptionStub .Lserror_el1_sp0, "SError EL1 SP0"

ExceptionStub .Lsync_el1_spx, "Sync EL1 SPx"
ExceptionStub .Lfiq_el1_spx,  "FIQ EL1 SPx"
ExceptionStub .Lserror_el1_spx, "SError EL1 SPx"


.global .Lirq_el0_64
.Lirq_el0_64:
    // Allocate space for ARM64Registers (272 bytes)
    sub     sp, sp, #272
    
    // Save x0-x30
    stp     x0, x1, [sp, #0]
    stp     x2, x3, [sp, #16]
    stp     x4, x5, [sp, #32]
    stp     x6, x7, [sp, #48]
    stp     x8, x9, [sp, #64]
    stp     x10, x11, [sp, #80]
    stp     x12, x13, [sp, #96]
    stp     x14, x15, [sp, #112]
    stp     x16, x17, [sp, #128]
    stp     x18, x19, [sp, #144]
    stp     x20, x21, [sp, #160]
    stp     x22, x23, [sp, #176]
    stp     x24, x25, [sp, #192]
    stp     x26, x27, [sp, #208]
    stp     x28, x29, [sp, #224]
    str     x30, [sp, #240]

    // Save SP_EL0
    mrs     x0, sp_el0
    str     x0, [sp, #248]
    
    mrs     x0, elr_el1
    str     x0, [sp, #256]
    
    mrs     x0, spsr_el1
    str     x0, [sp, #264]

    // Call C handler
    mov     x0, sp // Pass registers pointer
    bl      irq_handler_el0

    // Restore ELR and SPSR
    ldr     x0, [sp, #256]
    msr     elr_el1, x0
    ldr     x0, [sp, #264]
    msr     spsr_el1, x0
    
    // Restore SP_EL0
    ldr     x0, [sp, #248]
    msr     sp_el0, x0
    
    // Restore x0-x30
    ldp     x0, x1, [sp, #0]
    ldp     x2, x3, [sp, #16]
    ldp     x4, x5, [sp, #32]
    ldp     x6, x7, [sp, #48]
    ldp     x8, x9, [sp, #64]
    ldp     x10, x11, [sp, #80]
    ldp     x12, x13, [sp, #96]
    ldp     x14, x15, [sp, #112]
    ldp     x16, x17, [sp, #128]
    ldp     x18, x19, [sp, #144]
    ldp     x20, x21, [sp, #160]
    ldp     x22, x23, [sp, #176]
    ldp     x24, x25, [sp, #192]
    ldp     x26, x27, [sp, #208]
    ldp     x28, x29, [sp, #224]
    ldr     x30, [sp, #240]
    
    add     sp, sp, #272
    eret
ExceptionStub .Lfiq_el0_64,  "FIQ EL0 64"
ExceptionStub .Lserror_el0_64, "SError EL0 64"

ExceptionStub .Lsync_el0_32, "Sync EL0 32"
ExceptionStub .Lirq_el0_32,  "IRQ EL0 32"
ExceptionStub .Lfiq_el0_32,  "FIQ EL0 32"
ExceptionStub .Lserror_el0_32, "SError EL0 32"

// Real IRQ handler for EL1 SPx
.global .Lirq_el1_spx
.Lirq_el1_spx:
    // Allocate space for ARM64Registers (272 bytes)
    sub     sp, sp, #272
    
    // Save x0-x30
    stp     x0, x1, [sp, #0]
    stp     x2, x3, [sp, #16]
    stp     x4, x5, [sp, #32]
    stp     x6, x7, [sp, #48]
    stp     x8, x9, [sp, #64]
    stp     x10, x11, [sp, #80]
    stp     x12, x13, [sp, #96]
    stp     x14, x15, [sp, #112]
    stp     x16, x17, [sp, #128]
    stp     x18, x19, [sp, #144]
    stp     x20, x21, [sp, #160]
    stp     x22, x23, [sp, #176]
    stp     x24, x25, [sp, #192]
    stp     x26, x27, [sp, #208]
    stp     x28, x29, [sp, #224]
    str     x30, [sp, #240]

    // Save SP, ELR, SPSR
    mov     x0, sp
    add     x0, x0, #272 // Original SP
    str     x0, [sp, #248]
    
    mrs     x0, elr_el1
    str     x0, [sp, #256]
    
    mrs     x0, spsr_el1
    str     x0, [sp, #264]

    // Call C handler
    mov     x0, sp // Pass registers pointer
    bl      irq_handler_el1

    // Restore ELR and SPSR
    ldr     x0, [sp, #256]
    msr     elr_el1, x0
    ldr     x0, [sp, #264]
    msr     spsr_el1, x0
    
    // Restore x0-x30
    ldp     x0, x1, [sp, #0]
    ldp     x2, x3, [sp, #16]
    ldp     x4, x5, [sp, #32]
    ldp     x6, x7, [sp, #48]
    ldp     x8, x9, [sp, #64]
    ldp     x10, x11, [sp, #80]
    ldp     x12, x13, [sp, #96]
    ldp     x14, x15, [sp, #112]
    ldp     x16, x17, [sp, #128]
    ldp     x18, x19, [sp, #144]
    ldp     x20, x21, [sp, #160]
    ldp     x22, x23, [sp, #176]
    ldp     x24, x25, [sp, #192]
    ldp     x26, x27, [sp, #208]
    ldp     x28, x29, [sp, #224]
    ldr     x30, [sp, #240]
    
    add     sp, sp, #272
    eret

.global .Lsync_el0_64
.Lsync_el0_64:
    // Allocate space for ARM64Registers (272 bytes)
    sub     sp, sp, #272
    
    // Save x0-x30
    stp     x0, x1, [sp, #0]
    stp     x2, x3, [sp, #16]
    stp     x4, x5, [sp, #32]
    stp     x6, x7, [sp, #48]
    stp     x8, x9, [sp, #64]
    stp     x10, x11, [sp, #80]
    stp     x12, x13, [sp, #96]
    stp     x14, x15, [sp, #112]
    stp     x16, x17, [sp, #128]
    stp     x18, x19, [sp, #144]
    stp     x20, x21, [sp, #160]
    stp     x22, x23, [sp, #176]
    stp     x24, x25, [sp, #192]
    stp     x26, x27, [sp, #208]
    stp     x28, x29, [sp, #224]
    str     x30, [sp, #240]

    // Save SP_EL0
    mrs     x0, sp_el0
    str     x0, [sp, #248]
    
    mrs     x0, elr_el1
    str     x0, [sp, #256]
    
    mrs     x0, spsr_el1
    str     x0, [sp, #264]

    // Call C handler
    mov     x0, sp // Pass registers pointer
    bl      syscall_handler

    // Restore ELR and SPSR
    ldr     x0, [sp, #256]
    msr     elr_el1, x0
    ldr     x0, [sp, #264]
    msr     spsr_el1, x0
    
    // Restore SP_EL0
    ldr     x0, [sp, #248]
    msr     sp_el0, x0
    
    // Restore x0-x30
    ldp     x0, x1, [sp, #0]
    ldp     x2, x3, [sp, #16]
    ldp     x4, x5, [sp, #32]
    ldp     x6, x7, [sp, #48]
    ldp     x8, x9, [sp, #64]
    ldp     x10, x11, [sp, #80]
    ldp     x12, x13, [sp, #96]
    ldp     x14, x15, [sp, #112]
    ldp     x16, x17, [sp, #128]
    ldp     x18, x19, [sp, #144]
    ldp     x20, x21, [sp, #160]
    ldp     x22, x23, [sp, #176]
    ldp     x24, x25, [sp, #192]
    ldp     x26, x27, [sp, #208]
    ldp     x28, x29, [sp, #224]
    ldr     x30, [sp, #240]
    
    add     sp, sp, #272
    eret
