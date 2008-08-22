/*
 * Copyright (C) 2006 - 2008 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * @OLUXORG_LICENSE_HEADER_START@
 * @OLUXORG_LICENSE_HEADER_END@
 *
 * info.S -- OluxOS code for getting BIOS information
 *
 */


#include <ia32/platform.h>


.file       "info.S"




///////////////////////////////////////////////////////////////////////////////
// Code Segment                                                              //
///////////////////////////////////////////////////////////////////////////////
.text
.code16
.global     _info_start, __e820_count, __e820_data


_info_start:


    // Query E820 System Address Map
    xorl    %ebx, %ebx


    // Setup buffer
    pushw   %cs
    popw    %es
    movw    $__e820_data, %di


_next_record:


    // EAX = E820
    // ECX = Length of buffer
    // EDX = 'SMAP'
    movl    $0xe820, %eax
    movl    $24, %ecx
    movl    $0x534d4150, %edx


    // Do query e820
    int     $0x15
    jc      _e820_done


    // Move to next record
    addw    $24, %di
    incb    __e820_count
    cmp     $0, %ebx
    jne     _next_record


_e820_done:


    jmp     _pm_start


.align 4
__e820_count:
    .byte   0


__e820_data:
    .fill   (24 * 8), 1, 0



