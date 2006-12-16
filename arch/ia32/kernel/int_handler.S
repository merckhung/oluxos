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


//
// Pre-interrupt handling
//
#define IA32_SAVEALLREGS    \
        cld;                \
        pushl   %ds;        \
        pushl   %es;        \
        pushl   %eax;       \
        pushl   %ebx;       \
        pushl   %ecx;       \
        pushl   %edx;       \
        pushl   %ebp;       \
        pushl   %edi;       \
        pushl   %esi


//
// Post-interrupt handling
//
#define IA32_RESTOREALLREGS \
        popl    %esi;       \
        popl    %edi;       \
        popl    %ebp;       \
        popl    %edx;       \
        popl    %ecx;       \
        popl    %ebx;       \
        popl    %eax;       \
        popl    %es;        \
        popl    %ds


//
// Define
// Preliminary Interrupt Handler
// Preload interrupt vector into stack and then jump to common handler
//
.macro ia32_InstallInterruptHandler irqnum
    .global ia32_PreliminaryInterruptHandler_\irqnum
    .align 4
    ia32_PreliminaryInterruptHandler_\irqnum:
    movl    $\irqnum, -40(%esp)
    jmp     ia32_InterruptHandler
.endm





//
// Install Interrupt Handler
//
.irp irqnum, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15
ia32_InstallInterruptHandler \irqnum
.endr


//
// ia32_InterruptHandler -- Common entry for interrupt
//
.align 4
ia32_InterruptHandler:

    IA32_SAVEALLREGS

    // Get preload interrupt vector
    subl    $4, %esp
    call    ia32_IntHandleIRQ
    addl    $4, %esp
    jmp     ia32_InterruptReturn

ia32_InterruptReturn:

    IA32_RESTOREALLREGS
    iret


//
// ia32_ExceptionHandler -- Common entry for exception
//
.global ia32_ExceptionHandler
.align 4
ia32_ExceptionHandler:

    IA32_SAVEALLREGS
    // CALL routines
    jmp     ia32_ExceptionReturn

ia32_ExceptionReturn:

    IA32_RESTOREALLREGS
    addl    $4, %esp
    iret


