/*
 * Copyright (C) 2007 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * @OLUXORG_LICENSE_HEADER_START@
 * @OLUXORG_LICENSE_HEADER_END@
 *
 * io.c -- OluxOS IA32 Input/Output routines
 *
 */


void ia32_PtOutB( const unsigned char value, const unsigned int port ) {

    __asm__ __volatile__ (          \
        "outb   %%al, %%dx"         \
        :: "a" (value), "d" (port)  \
    );
}


unsigned char ia32_PtInB( const unsigned int port ) {

    unsigned char value;

    __asm__ __volatile__ (      \
        "mov    %1, %%dx\n"     \
        "inb    %%dx, %%al\n"   \
        "mov    %%al, %0\n"     \
        : "=m" (value)          \
        : "m" (port)            \
        : "al", "dx"            \
    );

    return value;
}


