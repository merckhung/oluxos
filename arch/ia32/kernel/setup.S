/*
 * Copyright (C) 2006 - 2007 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * @OLUXORG_LICENSE_HEADER_START@
 * @OLUXORG_LICENSE_HEADER_END@
 *
 * setup.S -- OluxOS setup code
 *
 */


.file       "setup.S"


STACK_SIZE=64


/******************************************************************************
 * Code Segment
 */
.text
.global     _setup, __KERNEL_CS, __KERNEL_DS, __USER_CS, __USER_DS, __ktask_seg, __utask_seg, KrnTSSD, UsrTSSD, __ldt_seg


_setup:


    // Setup our Global Segment Descriptors
    mov     $__gdt, %eax
    mov     %eax, (__gdtptr + 2)
    lgdt    (__gdtptr)


    // Setup Stack Pointer
    mov     $__stack + STACK_SIZE, %esp


    // Setup DS, ES, GS, FS
    mov     $__KERNEL_DS, %ax
    mov     %ax, %ds
    mov     %ax, %es
    mov     %ax, %fs
    mov     %ax, %gs
    mov     %ax, %ss


    // Initialize stack segment
    movl    $(__stack + STACK_SIZE), %esp


    // Reset flags
    pushl   $0
    popf


    // Reload CS and jump to C kernel entry
    .byte   0xea
    .long   krn_entry
    .word   __KERNEL_CS




/******************************************************************************
 * Global segment descriptor
 */
.data
.align 16


/*
 * Globel Descriptors
 */
__gdt:


    // 2 NULL segment descriptors
    .fill 2, 8, 0


    //
    // Kernel Code segment descriptor
    // Base address = 0x0000h
    // Limit(15:00) = 0xffffh   (0xfffff, 4Gb limit)
    // Limit(19:16) = 0xfh      
    // P            = 1b        (present in memory)
    // DPL          = 00b       (privilege level 0)
    // S            = 1b        (Code/Data segment)
    // Type         = 1010b     (code, execute/read)
    // AVL          = 0b        (reserved for OS)
    // D/B          = 1b        (32-bit)
    // G            = 1b        (4kb, limit unit)
    //
__KERNEL_CS = . - __gdt
    .word   0xffff
    .word   0x0000
    .word   0x9a00
    .word   0x00cf


    //
    // Kernel Data segment descriptor
    // Base address = 0x0000h
    // Limit(15:00) = 0xffffh   (0xfffff, 4Gb limit)
    // Limit(19:16) = 0xfh
    // P            = 1b        (present in memory)
    // DPL          = 00b       (privilege level 0)
    // S            = 1b        (Code/Data segment)
    // Type         = 0010b     (read/write)
    // AVL          = 0b
    // D/B          = 1b        (32-bit)
    // G            = 1b        (4kb, limit unit)
    //
__KERNEL_DS = . - __gdt
    .word   0xffff
    .word   0x0000
    .word   0x9200
    .word   0x00cf


    //
    // User Code segment descriptor
    // Base address = 0x0000h
    // Limit(15:00) = 0xffffh   (0xfffff, 4Gb limit)
    // Limit(19:16) = 0xfh      
    // P            = 1b        (present in memory)
    // DPL          = 11b       (privilege level 3)
    // S            = 1b        (Code/Data segment)
    // Type         = 1010b     (code, execute/read)
    // AVL          = 0b        (reserved for OS)
    // D/B          = 1b        (32-bit)
    // G            = 1b        (4kb, limit unit)
    //
__USER_CS = . - __gdt
    .word   0xffff
    .word   0x0000
    .word   0xfa00
    .word   0x00cf


    //
    // User Data segment descriptor
    // Base address = 0x0000h
    // Limit(15:00) = 0xffffh   (0xfffff, 4Gb limit)
    // Limit(19:16) = 0xfh
    // P            = 1b        (present in memory)
    // DPL          = 11b       (privilege level 3)
    // S            = 1b        (Code/Data segment)
    // Type         = 0010b     (read/write)
    // AVL          = 0b
    // D/B          = 1b        (32-bit)
    // G            = 1b        (4kb, limit unit)
    //
__USER_DS = . - __gdt
    .word   0xffff
    .word   0x0000
    .word   0xf200
    .word   0x00cf


    //
    // Task State Segment Descriptor for Kernel
    // Base address = uninitialized (0x0000h)
    // Limit(15:00) = 0xffffh
    // Limit(19:16) = 0xfh
    // P            = 1b
    // DPL          = 00b
    // Type         = 1001b
    // AVL          = 0b
    // G            = 1b
    //
__ktask_seg = . - __gdt
KrnTSSD:
    .word   0xffff
    .word   0x0000
    .word   0x8900
    .word   0x000f


    //
    // Task State Segment Descriptor for User
    // Base address = uninitialized (0x0000h)
    // Limit(15:00) = 0xffffh
    // Limit(19:16) = 0xfh
    // P            = 1b
    // DPL          = 00b
    // Type         = 1001b
    // AVL          = 0b
    // G            = 1b
    //
__utask_seg = . - __gdt
UsrTSSD:
    .word   0xffff
    .word   0x0000
    .word   0xe900
    .word   0x000f


    //
    // Local Segment Descriptor
    //
__ldt_seg = . - __gdt
    .word   0xffff
    .word   0x0000
    .word   0xe200
    .word   0x000f


__len_of_gdt = . - __gdt
__gdtptr:
    //
    // GDT limit    = __len_of_gdt - 1
    // GDT Base     = 0x00000000h
    //
    .word       __len_of_gdt - 1
    .word       0, 0


/******************************************************************************
 * Stack
 */
.common     __stack, STACK_SIZE


