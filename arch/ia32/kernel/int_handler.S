/*
 * Copyright (C) 2007 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * @OLUXORG_LICENSE_HEADER_START@
 * @OLUXORG_LICENSE_HEADER_END@
 *
 * int_handler.S -- OluxOS IA32 interrupt handler
 *
 */


.global ia32_IntHandler
.global _ia32_TmIntHandler, ticks
.global _ia32_KbIntHandler


ticks:      .long       0


ia32_IntHandler:

    // pop error code
    popl    %eax

    // Write some thing
    mov     $0xb809e, %esi
    incb    (%esi)
    mov     $0xb809f, %esi
    incb    (%esi)

    // return
    iret


_ia32_TmIntHandler:

    cli
    incl    ticks
    call    ia32_TmIntHandler
    addl    $4, %esp
    sti

    iret


_ia32_KbIntHandler:

    cli
    call    ia32_KbIntHandler
    //addl    $4, %esp
    sti

    iret


