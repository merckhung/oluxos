/*
 * setup.S -- OluxOS x86_64 kernel setup code
 */

#include <x86_64/platform.h>

.file "setup.S"

/******************************************************************************
 * Code Segment
 */
.text
.global _setup_start, __KERNEL_CS, __KERNEL_DS, __USER_CS, __USER_DS
.global pml4_table, pdpt_table, pd_table

.code32
_setup_start:
    // We are currently in 32-bit Protected Mode (CS loaded by pm.S)
    
    // 1. Clear page tables
    cld
    movl $pml4_table, %edi
    movl $0x3000, %ecx  // 3 tables * 4096 bytes = 12288 bytes = 0x3000 / 4 = 0xC00 dwords
    shrl $2, %ecx
    xorl %eax, %eax
    rep stosl

    // 2. Set up PML4 [0] -> PDPT
    movl $pml4_table, %eax
    movl $pdpt_table, %ebx
    orl $0x03, %ebx // Present, Read/Write
    movl %ebx, (%eax)
    
    // Set up PDPT [0] -> PD
    movl $pdpt_table, %eax
    movl $pd_table, %ebx
    orl $0x03, %ebx // Present, Read/Write
    movl %ebx, (%eax)

    // Set up PD [0] -> 2MB Page at 0x0
    movl $pd_table, %eax
    movl $0x00000083, %ebx // Present, Read/Write, Huge (2MB)
    movl %ebx, (%eax)

    // 3. Load CR3 with PML4
    movl $pml4_table, %eax
    movl %eax, %cr3

    // 4. Enable PAE and SSE (OSFXSR, OSXMMEXCPT)
    movl %cr4, %eax
    orl $(1 << 5), %eax // PAE
    orl $(1 << 9), %eax // OSFXSR
    orl $(1 << 10), %eax // OSXMMEXCPT
    movl %eax, %cr4

    // 5. Enable Long Mode (LME)
    movl $0xC0000080, %ecx // EFER MSR
    rdmsr
    orl $(1 << 8), %eax // LME
    wrmsr

    // 6. Enable Paging
    movl %cr0, %eax
    orl $(1 << 31), %eax // PG
    movl %eax, %cr0

    // 7. Load 64-bit GDT
    lgdt __gdtptr64

    // 8. Far jump to 64-bit code segment
    ljmp $0x08, $_setup_long_mode

.code64
_setup_long_mode:
    // Now we are in 64-bit Long Mode!
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    movw %ax, %ss

    // Initialize stack
    movq $(__stack + KRN_STACK_SZ), %rsp

    // Reset flags
    pushq $0
    popfq

    // Call C kernel entry
    call krn_entry

    // Halt
1:  cli
    hlt
    jmp 1b

/******************************************************************************
 * Global segment descriptor
 */
.align 16
__gdt64:
    // 0: NULL segment descriptor
    .quad 0

__KERNEL_CS = . - __gdt64
    // 8: Kernel Code segment (64-bit)
    .word 0x0000
    .word 0x0000
    .word 0x9a00
    .word 0x00af // L=1, D=0

__KERNEL_DS = . - __gdt64
    // 16: Kernel Data segment
    .word 0x0000
    .word 0x0000
    .word 0x9200
    .word 0x0000 // In 64-bit mode, only P and DPL matter, but let's keep it simple

__USER_CS = . - __gdt64
    // 24: User Code segment (64-bit)
    .word 0x0000
    .word 0x0000
    .word 0xfa00 // DPL=3
    .word 0x00af // L=1, D=0

__USER_DS = . - __gdt64
    // 32: User Data segment
    .word 0x0000
    .word 0x0000
    .word 0xf200 // DPL=3
    .word 0x0000

__len_of_gdt64 = . - __gdt64

__gdtptr64:
    .word __len_of_gdt64 - 1
    .quad __gdt64

/******************************************************************************
 * Page Tables (Aligned to 4KB)
 */
.section .bss
.align 4096
pml4_table:
    .fill 4096, 1, 0
pdpt_table:
    .fill 4096, 1, 0
pd_table:
    .fill 4096, 1, 0

/******************************************************************************
 * Stack
 */
.common __stack, KRN_STACK_SZ



