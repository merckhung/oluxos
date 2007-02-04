/*
 * Copyright (C) 2006 - 2007 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * @OLUXORG_LICENSE_HEADER_START@
 * @OLUXORG_LICENSE_HEADER_END@
 *
 * int_handler.S -- OluxOS IA32 interrupt handler
 *
 */
#include <ia32/interrupt.h>


//
// Pre-interrupt handling
//
.macro ia32_SaveAllRegs
    cld
    pushl   %ds
    pushl   %es
    pushl   %eax
    pushl   %ebx
    pushl   %ecx
    pushl   %edx
    pushl   %ebp
    pushl   %edi
    pushl   %esi
.endm


//
// Restore all register after interrupt handler
//
.macro ia32_RestoreAllRegs
    popl    %esi
    popl    %edi
    popl    %ebp
    popl    %edx
    popl    %ecx
    popl    %ebx
    popl    %eax
    popl    %es
    popl    %ds
.endm


//
// Define
// Preliminary Interrupt Handler
// Preload interrupt vector into stack and then jump to common handler
//
.macro ia32_InstallInterruptHandler
    .global ia32_PreliminaryInterruptHandler_\@
    .align 4
    ia32_PreliminaryInterruptHandler_\@:
    movl    $\@, -40(%esp)
    jmp     ia32_InterruptHandler
.endm






//
// Install Interrupt Handler
//
.rept NR_INTERRUPT
ia32_InstallInterruptHandler
.endr


//
// ia32_InterruptHandler -- Common entry for interrupt
//
.align 4
ia32_InterruptHandler:

    ia32_SaveAllRegs
    subl    $4, %esp                // Get preload interrupt vector
    call    ia32_IntHandleIRQ
    addl    $4, %esp                // Discard vector
    jmp     ia32_InterruptReturn

ia32_InterruptReturn:
    ia32_RestoreAllRegs

    iret


//
// ia32_divide_error -- Handle exception 0 #DE
//
ia32_divide_error:

    movl    $0xb83be, %esi
    movb    $'0', (%esi)
    //
    // FIXME: exception handle
    //
    jmp     ia32_ExceptionReturn


//
// ia32_debug_error -- Handle exception 1 #DB
//
ia32_debug:

    movl    $0xb83be, %esi
    movb    $'1', (%esi)
    //
    // FIXME: exception handle
    //
    jmp     ia32_ExceptionReturn

//
// ia32_nmi -- Handle exception 2
//
ia32_nmi:

    movl    $0xb83be, %esi
    movb    $'2', (%esi)
    //
    // FIXME: exception handle
    //
    jmp     ia32_ExceptionReturn


//
// ia32_breakpoint -- Handle exception 3 #BP
//
ia32_breakpoint:

    movl    $0xb83be, %esi
    movb    $'3', (%esi)
    //
    // FIXME: exception handle
    //
    jmp     ia32_ExceptionReturn


//
// ia32_overflow -- Handle exception 4 #OF
//
ia32_overflow:

    movl    $0xb83be, %esi
    movb    $'4', (%esi)
    //
    // FIXME: exception handle
    //
    jmp     ia32_ExceptionReturn


//
// ia32_bound_range_exceeded -- Handle exception 5 #BR
//
ia32_bound_range_exceeded:

    movl    $0xb83be, %esi
    movb    $'5', (%esi)
    //
    // FIXME: exception handle
    //
    jmp     ia32_ExceptionReturn

//
// ia32_invalid_opcode -- Handle exception 6 #UD
//
ia32_invalid_opcode:

    movl    $0xb83be, %esi
    movb    $'6', (%esi)
    //
    // FIXME: exception handle
    //
    jmp     ia32_ExceptionReturn


//
// ia32_device_not_available -- Handle exception 7 #NM
//
ia32_device_not_available:

    movl    $0xb83be, %esi
    movb    $'7', (%esi)
    //
    // FIXME: exception handle
    //
    jmp     ia32_ExceptionReturn


//
// ia32_double_fault -- Handle exception 8 #DF
//
ia32_double_fault:

    movl    $0xb83be, %esi
    movb    $'8', (%esi)
    //
    // FIXME: exception handle
    //
    jmp     ia32_ExceptionReturn


//
// ia32_coprocessor_segment_overrun -- Handle exception 9
//
ia32_coprocessor_segment_overrun:

    movl    $0xb83be, %esi
    movb    $'9', (%esi)
    //
    // FIXME: exception handle
    //
    jmp     ia32_ExceptionReturn

//
// ia32_invalid_tss -- Handle exception 10 #TS
//
ia32_invalid_tss:

    movl    $0xb83bc, %esi
    movb    $'1', (%esi)
    movl    $0xb83be, %esi
    movb    $'0', (%esi)
    //
    // FIXME: exception handle
    //
    jmp     ia32_ExceptionReturn


//
// ia32_segment_not_present -- Handle exception 11 #NP
//
ia32_segment_not_present:

    movl    $0xb83bc, %esi
    movb    $'1', (%esi)
    movl    $0xb83be, %esi
    movb    $'1', (%esi)
    //
    // FIXME: exception handle
    //
    jmp     ia32_ExceptionReturn


//
// ia32_stack_fault -- Handle exception 12 #SS
//
ia32_stack_fault:

    movl    $0xb83bc, %esi
    movb    $'1', (%esi)
    movl    $0xb83be, %esi
    movb    $'2', (%esi)
    //
    // FIXME: exception handle
    //
    jmp     ia32_ExceptionReturn


//
// ia32_general_protection_exception -- Handle exception 13 #NP
//
ia32_general_protection_exception:

    movl    $0xb83bc, %esi
    movb    $'1', (%esi)
    movl    $0xb83be, %esi
    movb    $'3', (%esi)
    //
    // FIXME: exception handle
    //
    jmp     ia32_ExceptionReturn






ia32_ExceptionReturn:

    iret



//
// ia32_ExceptionHandler -- Common entry for exception
//
.global ia32_ExceptionHandler
.align 4
ia32_ExceptionHandler:

    cli
    movl    $0xb83bc, %esi
    incb    (%esi)
    movl    $0xb83bd, %esi
    incb    (%esi)
    sti
    iret


