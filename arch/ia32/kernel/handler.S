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
    movl    %eax, SavedAllRegs
    movl    %ecx, SavedAllRegs+4
    movl    %edx, SavedAllRegs+8
    movl    %ebx, SavedAllRegs+12
    movl    %ebp, SavedAllRegs+20
    movl    %esi, SavedAllRegs+24
    movl    %edi, SavedAllRegs+28
    movw    %ds, SavedAllRegs+48
    movw    %es, SavedAllRegs+52
    movw    %fs, SavedAllRegs+56
    movw    %gs, SavedAllRegs+60
    popl    %ebx
    movl    %ebx, SavedErrorCode
    popl    %ebx
    movl    %ebx, SavedAllRegs+32
    popl    %ebx
    movl    %ebx, SavedAllRegs+40
    popl    %ebx
    movl    %ebx, SavedAllRegs+36
    movw    %ss, SavedAllRegs+44
    movl    %esp, SavedAllRegs+16
.endm


.macro SaveAllRegsOld
    cld
    pushl   %es
    pushl   %ss
    pushl   %ds
    pushl   %fs
    pushl   %gs
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
    movl    SavedAllRegs+16, %esp
    movw    SavedAllRegs+44, %ss
    movw    SavedAllRegs+60, %gs
    movw    SavedAllRegs+56, %fs
    movw    SavedAllRegs+52, %es
    movw    SavedAllRegs+48, %ds
    movl    SavedAllRegs+28, %edi
    movl    SavedAllRegs+24, %esi
    movl    SavedAllRegs+20, %ebp
    movl    SavedAllRegs+12, %ebx
    movl    SavedAllRegs+8, %edx
    movl    SavedAllRegs+4, %ecx
    movl    SavedAllRegs+36, %eax
    pushl   %eax
    movl    SavedAllRegs+40, %eax
    pushl   %eax
    movl    SavedAllRegs+32, %eax
    pushl   %eax
    movl    SavedAllRegs, %eax
.endm


.macro RestoreAllRegsOld
    popl    %edi
    popl    %esi
    popl    %ebp
    popl    %ebx
    popl    %edx
    popl    %ecx
    popl    %eax
    popl    %gs
    popl    %fs
    popl    %ds
    popl    %ss
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
    movl    $\@, -52(%esp)
    lea     -52(%esp), %ebx
    jmp     InterruptHandler
.endm



//
// Save All Registers
//
.align 4
SavedErrorCode:
.long   0


.align 4
SavedAllRegs:
.rept 16
.long   0
.endr



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

    SaveAllRegsOld
    subl    $4, %esp                // Get preload interrupt vector
    call    IntHandleIRQ
    addl    $4, %esp                // Discard vector
    jmp     InterruptReturn

InterruptReturn:
    RestoreAllRegsOld

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

    movl    $0xb83bc, %esi
    incb    (%esi)
    movl    $0xb83bd, %esi
    incb    (%esi)
    iret


