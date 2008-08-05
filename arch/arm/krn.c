/*
 * Copyright (C) 2006 - 2008 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * @OLUXORG_LICENSE_HEADER_START@
 * @OLUXORG_LICENSE_HEADER_END@
 *
 * krn.c -- OluxOS ARM kernel entry point
 *
 */



void KernelEntry( void ) {

    __asm__ __volatile__ (
    
        "ldr    fp, =0x11447766\n"
        "b      .\n"
    );
}



