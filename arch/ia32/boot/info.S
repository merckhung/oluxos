/*
 * Copyright (C) 2006 - 2008 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * File: info.S
 * Description:
 *	OluxOS code for getting BIOS information
 *
 */


#include <ia32/platform.h>
#include <ia32/bios.h>


.file       "info.S"



///////////////////////////////////////////////////////////////////////////////
// Code Segment                                                              //
///////////////////////////////////////////////////////////////////////////////
.text
.code16
.global     _SystemInfo, __e820_count, __e820_data


_SystemInfo:


    // Query E820 System Address Map
    xorl    %ebx, %ebx
    movw    %bx, %es


    // Clear counter
    movl    $0, (E820_COUNT)


    // Setup buffer pointer
    movl    $E820_BASE, %edi
    

_next_record:


    // EAX = E820
    // ECX = Length of buffer
    // EDX = 'SMAP'
    movl    $E820_FUNC, %eax
    movl    $E820_SZ_RECORD, %ecx
    movl    $E820_MAGIC, %edx


    // Do query e820
    int     $0x15
    jc      _e820_done


    // Move to next record
    addw    $24, %di
    incb    (E820_COUNT)
    cmp     $0, %ebx
    jne     _next_record


_e820_done:


    jmp     _pm_start


