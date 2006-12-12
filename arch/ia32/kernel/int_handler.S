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

.global _ia32_KbIntHandler
.global ia32_InterruptHandler
.global ia32_ExceptionHandler


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
        pushl   %esi;


#define IA32_RESTOREALLREGS \
        popl    %esi;       \
        popl    %edi;       \
        popl    %ebp;       \
        popl    %edx;       \
        popl    %ecx;       \
        popl    %ebx;       \
        popl    %eax;       \
        popl    %es;        \
        popl    %ds;



//
// ia32_InterruptHandler -- Common entry for interrupt
//
ia32_InterruptHandler:

    IA32_SAVEALLREGS
    call    ia32_TmIntHandler
    jmp     ia32_InterruptReturn

ia32_InterruptReturn:

    IA32_RESTOREALLREGS
    iret


//
// ia32_ExceptionHandler -- Common entry for exception
//
ia32_ExceptionHandler:

    IA32_SAVEALLREGS
    // CALL routines
    jmp     ia32_ExceptionReturn

ia32_ExceptionReturn:

    IA32_RESTOREALLREGS
    addl    $4, %esp
    iret


_ia32_KbIntHandler:

    cli
    call    ia32_KbIntHandler
    addl    $4, %esp
    sti

    iret


