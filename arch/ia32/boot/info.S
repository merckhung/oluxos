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


.file       "info.S"




///////////////////////////////////////////////////////////////////////////////
// Code Segment                                                              //
///////////////////////////////////////////////////////////////////////////////
.text
.code16
.global     _info_start


_info_start:


    // Query E820 System Address Map
    movl    $0xe820, %eax
    xorl    %ebx, %ebx
    movl    $20, %ecx
    movl    $0x534d4150, %edx       // 'SMAP'


    // Setup buffer
    pushw   %cs
    popw    %es
    movw    $__e820_data, %di


    // Do query e820
    int     $0x15
    jc      _e820_err

    
    // Check return status
    cmpl    $0x534d4150, %eax
    jnz     _e820_err     


    // All done?
    cmpl    $0, %ecx
    jz      _e820_done



_e820_done:
_e820_err:


    jmp     _pm_start


__e820_data:
    .fill   20, 1, 0



