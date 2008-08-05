/*
 * Copyright (C) 2006 - 2008 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * @OLUXORG_LICENSE_HEADER_START@
 * @OLUXORG_LICENSE_HEADER_END@
 *
 * boot.S -- OluxOS bootstrap code
 *
 *  1. initialize registers
 *  2. print booting message
 *  3. setup global segment descriptors
 *  4. enable gate A20
 *  5. enable protected mode
 *  6. jump to protected mode
 * 
 * LABEL    : Subroutine
 * _LABEL   : Replacement of flow control
 * __LABEL  : Replacement of variable
 *
 */


#include <ia32/platform.h>


.file       "boot.S"




///////////////////////////////////////////////////////////////////////////////
// Code Segment                                                              //
///////////////////////////////////////////////////////////////////////////////
.text
.code16
.global     _SystemBoot, __krnsecs


_SystemBoot:

    jmp     _bpb
    nop

    .byte   0
    .long   0
    .long   0
    .word   0
    .byte   0
    .byte   0
    .word   0


_bpb:

    // Setup Buffer after MBR
    movw    $KRN_BOOT_SEG, %ax
    movw    %ax, %es
    movw    $KRN_BLOCK_SZ, %bx

    // Setup Int13
    movb    $0x02, %ah
    movb    __krnsecs, %al
    movw    $0x0002, %cx
    movw    $0x0080, %dx

    // Read rest code of kernel
    int     $0x13


    jmp     _info_start


.fill   467, 1, 0

__krnsecs:
    .byte   0

.word   0xaa55


