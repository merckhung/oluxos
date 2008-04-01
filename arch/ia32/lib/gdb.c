/*
 * Copyright (C) 2006 -  2007 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * @OLUXORG_LICENSE_HEADER_START@
 * @OLUXORG_LICENSE_HEADER_END@
 *
 * gdb.c -- OluxOS IA32 Remote GDB Routines
 *
 */
#include <types.h>
#include <clib.h>
#include <ia32/io.h>
#include <ia32/gdb.h>
#include <driver/sercon.h>


void GdbInit( void ) {

    void ScInit( void );
}


void GdbPutChar( char c ) {

    ScPutChar( c );
}


int GdbGetChar( void ) {

    return ScGetChar();
}


void GdbExceptionHandler( int exception_number, void *exception_address ) {



}


