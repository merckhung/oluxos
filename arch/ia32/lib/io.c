/*
 * Copyright (C) 2006 - 2007 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * @OLUXORG_LICENSE_HEADER_START@
 * @OLUXORG_LICENSE_HEADER_END@
 *
 * io.c -- OluxOS IA32 Input/Output routines
 *
 */
#include <types.h>


//
// IoOutByte
//
// Input:
//  value   : Value to write
//  port    : IO port number to write
//
// Return:
//  None
//
// Description:
//  Write value in byte to IO port
//
void IoOutByte( const __u8 value, const __u16 port ) {

    __asm__ __volatile__ (          \
        "outb   %%al, %%dx\n"       \
        :: "a" (value), "d" (port)  \
    );
}


//
// IoInByte
//
// Input:
//  port    : IO port number to write
//
// Return:
//  IO port value in byte
//
// Description:
//  Read value in byte from IO port
//
__u8 IoInByte( const __u16 port ) {

    __u8 value;

    __asm__ __volatile__ (      \
        "inb    %%dx, %%al\n"   \
        : "=a" (value)          \
        : "d" (port)            \
    );

    return value;
}


//
// IoOutWord
//
// Input:
//  value   : Value to write
//  port    : IO port number to write
//
// Return:
//  None
//
// Description:
//  Write value in word to IO port
//
void IoOutWord( const __u16 value, const __u16 port ) {

    __asm__ __volatile__ (          \
        "outw   %%ax, %%dx\n"       \
        :: "a" (value), "d" (port)  \
    );
}


//
// IoInWord
//
// Input:
//  port    : IO port number to write
//
// Return:
//  IO port value in word
//
// Description:
//  Read value in word from IO port
//
__u16 IoInWord( const __u16 port ) {

    __u16 value;

    __asm__ __volatile__ (      \
        "inw    %%dx, %%ax\n"   \
        : "=a" (value)          \
        : "d" (port)            \
    );

    return value;
}


//
// IoOutDWord
//
// Input:
//  value   : Value to write
//  port    : IO port number to write
//
// Return:
//  None
//
// Description:
//  Write value in double word to IO port
//
void IoOutDWord( const __u32 value, const __u16 port ) {

    __asm__ __volatile__ (          \
        "outl   %%eax, %%dx\n"      \
        :: "a" (value), "d" (port)  \
    );
}


//
// IoInDWord
//
// Input:
//  port    : IO port number to write
//
// Return:
//  IO port value in double word
//
// Description:
//  Read value in double word from IO port
//
__u32 IoInDWord( const __u16 port ) {

    __u32 value;

    __asm__ __volatile__ (      \
        "inl   %%dx, %%eax\n"   \
        : "=a" (value)          \
        : "d" (port)            \
    );

    return value;
}


