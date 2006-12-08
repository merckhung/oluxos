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
#include <ia32/types.h>


void ia32_IoOutByte( const __u8 value, const __u16 port ) {

    __asm__ __volatile__ (          \
        "outb   %%al, %%dx\n"       \
        :: "a" (value), "d" (port)  \
    );
}


__u8 ia32_IoInByte( const __u16 port ) {

    __u8 value;

    __asm__ __volatile__ (      \
        "inb    %%dx, %%al\n"   \
        : "=a" (value)          \
        : "d" (port)            \
    );

    return value;
}


void ia32_IoOutWord( const __u16 value, const __u16 port ) {

    __asm__ __volatile__ (          \
        "outw   %%ax, %%dx\n"       \
        :: "a" (value), "d" (port)  \
    );
}


__u16 ia32_IoInWord( const __u16 port ) {

    __u16 value;

    __asm__ __volatile__ (      \
        "inw    %%dx, %%ax\n"   \
        : "=a" (value)          \
        : "d" (port)            \
    );

    return value;
}


void ia32_IoOutDWord( const __u32 value, const __u16 port ) {

    __asm__ __volatile__ (          \
        "outl   %%eax, %%dx\n"      \
        :: "a" (value), "d" (port)  \
    );
}


__u32 ia32_IoInDWord( const __u16 port ) {

    __u32 value;

    __asm__ __volatile__ (      \
        "inl   %%dx, %%eax\n"  \
        : "=a" (value)          \
        : "d" (port)            \
    );

    return value;
}


