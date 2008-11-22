/*
 * Copyright (C) 2006 - 2008 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * File: pm.S
 * Description:
 *	OluxOS setup code of protected mode
 *
 */


#include <ia32/platform.h>


.file       "pm.S"




///////////////////////////////////////////////////////////////////////////////
// Code Segment                                                              //
///////////////////////////////////////////////////////////////////////////////
.text
.code16
.global     _pm_start


_pm_start:


    // Calculate offset of 32bit code start
    xorl    %eax, %eax
    movw    %cs, %ax
    shll    $4, %eax
    addl    $_32bit_mode_start, %eax
    movl    %eax, __code32


    // Convert to linear address
    xorl    %eax, %eax
    movw    %cs, %ax
    shll    $4, %eax
    addl    $__pregdt, %eax
    movl    %eax, (__pregdtptr + 2)
    lgdt    __pregdtptr


    // Disbale interrupt
    cli

    
    // Enable Gate A20
    inb     $0x92, %al
    orb     $0x02, %al
    outb    %al, $0x92


    // Enable protected mode
    movl    %cr0, %eax
    orl     $0x01, %eax
    movl    %eax, %cr0


    // jmpi instruction
    .byte   0x66, 0xea
__code32:
    .long   0
    .word   __code_seg


/*
 * Globel Descriptors
 */
.align 16
__pregdt:


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
__code_seg = . - __pregdt
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
__data_seg = . - __pregdt
    .word   0xffff
    .word   0x0000
    .word   0x9200
    .word   0x00cf
    __len_of_pregdt = . - __pregdt


__pregdtptr:
    //
    // GDT limit    = __len_of_pregdt - 1
    // GDT Base     = 0x00000000h
    //
    .word       __len_of_pregdt - 1
    .word       0, 0




///////////////////////////////////////////////////////////////////////////////
// Protected Mode 32bit Code Segment                                         //
///////////////////////////////////////////////////////////////////////////////
.text
.code32
_32bit_mode_start:


    // Calculate the Length of Kernel Code
    xorl    %ebx, %ebx
    movb    __KrnImgNrSecs, %bl
    movl    $KRN_BLOCK_SZ, %eax
    mull    %ebx
    movl    %eax, %ecx

    movl    $__krnimg_start, %eax
    movw    $KRN_BOOT_ADDR, %bx
    subw    %bx, %ax     
    

    // Get the length we need to know
    subl    %eax, %ecx


    // Align 4 bytes
    addl    $4, %ecx
    andb    $0xfc, %cl


    //
    // Setup DS, ES, GS, FS for 32bit mode
    //
    movw    $__data_seg, %ax
    movw    %ax, %ds
    movw    %ax, %es
    movw    %ax, %gs
    movw    %ax, %fs


    //
    // Copy Main Kernel Code to 1MB
    //
    // ESI = Linear address of Main Kernel Binary (Align 16 bytes)
    // EDI = Linear address of KRN_LOAD_ADDR (1MB)
    // ECX = Rest of length of kernel code in byte
    //
    movl    $__krnimg_start, %eax
    testb   $0x0f, %al
    jz      aldy_align

    andb    $0xf0, %al
    addl    $0x10, %eax
aldy_align:
    
    movl    %eax, %esi
    movl    $KRN_LOAD_ADDR, %edi
    rep     movsl


    //
    // Jump to _setup_start
    //
    jmp     KRN_LOAD_ADDR


    .word   0xaa55


__krnimg_start:




