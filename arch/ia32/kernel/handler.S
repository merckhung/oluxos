/*
 * Copyright (C) 2006 - 2007 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * @OLUXORG_LICENSE_HEADER_START@
 * @OLUXORG_LICENSE_HEADER_END@
 *
 * handler.S -- OluxOS IA32 interrupt handler
 *
 */
#include <ia32/interrupt.h>


.global divide_error, debug, nmi, breakpoint, overflow, bound_range_exceeded
.global invalid_opcode, device_not_available, double_fault, coprocessor_segment_overrun
.global invalid_tss, segment_not_present, stack_fault, general_protection_exception


//
// Pre-interrupt handling
//
.macro SaveAllRegs
    cld
    pushl   %es
    pushl   %ds
    pushl   %eax
    pushl   %ecx
    pushl   %edx
    pushl   %ebx
    pushl   %ebp
    pushl   %esi
    pushl   %edi
.endm


//
// Restore all register after interrupt handler
//
.macro RestoreAllRegs
    popl    %edi
    popl    %esi
    popl    %ebp
    popl    %ebx
    popl    %edx
    popl    %ecx
    popl    %eax
    popl    %ds
    popl    %es
.endm


//
// Define
// Preliminary Interrupt Handler
// Preload interrupt vector into stack and then jump to common handler
//
.macro InstallInterruptHandler
    .global PreliminaryInterruptHandler_\@
    .align 4
    PreliminaryInterruptHandler_\@:
    movl    $\@, -40(%esp)
    jmp     InterruptHandler
.endm






//
// Install Interrupt Handler
//
.rept NR_INTERRUPT
InstallInterruptHandler
.endr


//
// InterruptHandler -- Common entry for interrupt
//
.align 4
InterruptHandler:

    SaveAllRegs
    subl    $4, %esp                // Get preload interrupt vector
    call    IntHandleIRQ

    mov     (%esp), %eax
    addl    $4, %esp                // Discard vector
    testl   $-1, %eax               // Check IRQ == Timer
    jnz     NotTimer


    //
    // Save current state of running process,
    // then reschedule, and then restart process
    //
    call    TskReschedule
    jmp     InterruptReturn


NotTimer:
    jmp     InterruptReturn


InterruptReturn:
    RestoreAllRegs

    iret


//
// divide_error -- Handle exception 0 #DE
//
divide_error:

    movl    $0xb83be, %esi
    movb    $'0', (%esi)
    //
    // FIXME: exception handle
    //
    jmp     ExceptionReturn


//
// debug_error -- Handle exception 1 #DB
//
debug:

    movl    $0xb83be, %esi
    movb    $'1', (%esi)
    //
    // FIXME: exception handle
    //
    jmp     ExceptionReturn

//
// nmi -- Handle exception 2
//
nmi:

    movl    $0xb83be, %esi
    movb    $'2', (%esi)
    //
    // FIXME: exception handle
    //
    jmp     ExceptionReturn


//
// breakpoint -- Handle exception 3 #BP
//
breakpoint:

    movl    $0xb83be, %esi
    movb    $'3', (%esi)
    //
    // FIXME: exception handle
    //
    jmp     ExceptionReturn


//
// overflow -- Handle exception 4 #OF
//
overflow:

    movl    $0xb83be, %esi
    movb    $'4', (%esi)
    //
    // FIXME: exception handle
    //
    jmp     ExceptionReturn


//
// bound_range_exceeded -- Handle exception 5 #BR
//
bound_range_exceeded:

    movl    $0xb83be, %esi
    movb    $'5', (%esi)
    //
    // FIXME: exception handle
    //
    jmp     ExceptionReturn

//
// invalid_opcode -- Handle exception 6 #UD
//
invalid_opcode:

    movl    $0xb83be, %esi
    movb    $'6', (%esi)
    //
    // FIXME: exception handle
    //
    jmp     ExceptionReturn


//
// device_not_available -- Handle exception 7 #NM
//
device_not_available:

    movl    $0xb83be, %esi
    movb    $'7', (%esi)
    //
    // FIXME: exception handle
    //
    jmp     ExceptionReturn


//
// double_fault -- Handle exception 8 #DF
//
double_fault:

    movl    $0xb83be, %esi
    movb    $'8', (%esi)
    //
    // FIXME: exception handle
    //
    jmp     ExceptionReturn


//
// coprocessor_segment_overrun -- Handle exception 9
//
coprocessor_segment_overrun:

    movl    $0xb83be, %esi
    movb    $'9', (%esi)
    //
    // FIXME: exception handle
    //
    jmp     ExceptionReturn

//
// invalid_tss -- Handle exception 10 #TS
//
invalid_tss:

    movl    $0xb83bc, %esi
    movb    $'1', (%esi)
    movl    $0xb83be, %esi
    movb    $'0', (%esi)
    //
    // FIXME: exception handle
    //
    jmp     ExceptionReturn


//
// segment_not_present -- Handle exception 11 #NP
//
segment_not_present:

    movl    $0xb83bc, %esi
    movb    $'1', (%esi)
    movl    $0xb83be, %esi
    movb    $'1', (%esi)
    //
    // FIXME: exception handle
    //
    jmp     ExceptionReturn


//
// stack_fault -- Handle exception 12 #SS
//
stack_fault:

    movl    $0xb83bc, %esi
    movb    $'1', (%esi)
    movl    $0xb83be, %esi
    movb    $'2', (%esi)
    //
    // FIXME: exception handle
    //
    jmp     ExceptionReturn


//
// general_protection_exception -- Handle exception 13 #NP
//
general_protection_exception:

    movl    $0xb83bc, %esi
    movb    $'1', (%esi)
    movl    $0xb83be, %esi
    movb    $'3', (%esi)
    //
    // FIXME: exception handle
    //
    jmp     ExceptionReturn






ExceptionReturn:

    iret



//
// ExceptionHandler -- Common entry for exception
//
.global ExceptionHandler
.align 4
ExceptionHandler:

    cli
    movl    $0xb83bc, %esi
    incb    (%esi)
    movl    $0xb83bd, %esi
    incb    (%esi)
    sti
    iret


