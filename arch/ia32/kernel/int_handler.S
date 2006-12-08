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


.global ia32_IntHandler, ia32_TmIntHandler


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


