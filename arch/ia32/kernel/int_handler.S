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
    movl    $12345678, %ebx
    movl    $87654321, %ecx
    movl    $0xfafafafa, %edx

    // stop here
CHECK:
    jmp     CHECK

    // return
    iret



