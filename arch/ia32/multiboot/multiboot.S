/*
 * Copyright (C) 2006 - 2007 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * @OLUXORG_LICENSE_HEADER_START@
 * @OLUXORG_LICENSE_HEADER_END@
 *
 * multiboot.S -- OluxOS boot code
 *
 * This is 32bit mode setup code after multiboot loader
 *  1. setup global segment descriptors
 *  2. setup stack base and pointer
 *  3. jump to C kernel entry
 * 
 */


.file       "boot.S"


MULTIBOOT_MAGIC=0x1badb002
MULTIBOOT_FLAGS=0x00000003

STACK_SIZE=4096


/******************************************************************************
 * Code Segment
 */
.text
.global     _multiboot, __code_seg, __data_seg


_multiboot:
    jmp     _start


.align  4
    .long   MULTIBOOT_MAGIC
    .long   MULTIBOOT_FLAGS
    .long   -(MULTIBOOT_MAGIC + MULTIBOOT_FLAGS)


_start:


    // Setup our Global Segment Descriptors
    mov     $__gdt, %eax
    mov     %eax, (__gdtptr + 2)
    lgdt    (__gdtptr)


    // Setup Stack Pointer
    mov     $__stack + STACK_SIZE, %esp


    // Setup DS, ES, GS, FS
    mov     $__data_seg, %ax
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
    .long   ia32_krn_entry
    .word   __code_seg




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
    // Code segment descriptor
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
__code_seg = . - __gdt
    .word   0xffff
    .word   0x0000
    .word   0x9a00
    .word   0x00cf

    //
    // Data segment descriptor
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
__data_seg = . - __gdt
    .word   0xffff
    .word   0x0000
    .word   0x9200
    .word   0x00cf
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


