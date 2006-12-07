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


ia32_IntHandler:

    // pop error code
    popl    %eax

    // Write some thing
    mov     $0xb8000, %esi
    incb    (%esi)
    mov     $0xb8001, %esi
    incb    (%esi)

CHECK:
    jmp     CHECK

    // return
    iret



