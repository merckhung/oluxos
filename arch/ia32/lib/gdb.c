/*
 * Copyright (C) 2006 -  2008 Olux Organization All rights reserved.
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
#include <driver/serial.h>


static s8 GdbInBuf[ GDB_BUF_LEN ];
static s8 GdbOutBuf[ GDB_BUF_LEN ];


void GdbInit( void ) {

    SrInit();
}


void GdbPutChar( s8 c ) {

    SrPutChar( c );
}


s8 GdbGetChar( void ) {

    return SrGetChar();
}


void GdbGetPacket( s8 *buf ) {


}


void GdbSendPacket( s8 *buf ) {


}


void GdbExceptionHandler( u32 exception_number, void *exception_address ) {


}


