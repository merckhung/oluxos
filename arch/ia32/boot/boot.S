/*
 * Copyright (C) 2006 - 2008 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * File: boot.S
 * Description:
 *	OluxOS boot sector code
 *
 */


#include <ia32/platform.h>
#include <ia32/bios.h>


.file       "boot.S"


///////////////////////////////////////////////////////////////////////////////
// Code Segment                                                              //
///////////////////////////////////////////////////////////////////////////////
.text
.code16
.global _SystemBoot, __KrnImgNrSecs


_SystemBoot:
    jmp     _SkipParams


__KrnImgNrSecs:
    .byte   0


_SkipParams:


    // Setup Buffer after MBR
    movw    $KRN_BOOT_SEG, %ax
    movw    %ax, %es
    movw    $KRN_BLOCK_SZ, %bx


    // Setup Int13
    movb    $HDD_READ, %ah
    movb    __KrnImgNrSecs, %al
    movw    $0x0002, %cx
    movw    $0x0080, %dx


    // Read rest code of kernel
    int     $0x13


    jmp     _SystemInfo


.fill   483, 1, 0

.word   0xaa55


