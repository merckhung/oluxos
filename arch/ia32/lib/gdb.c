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


void GdbInit( void ) {

    // Turn off Interrupt
    IoOutByte( 0x00, COMIO + 1 );

    // DLAB ON
    IoOutByte( 0x80, COMIO + 3 );

    // Baud rate = 19200
    // Divisor Low Byte
    IoOutByte( 0x06, COMIO );

    // Divisor High Byte
    IoOutByte( 0x00, COMIO + 1 );

    // DLAB = OFF, 8 Bits, No Parity, 1 Stop Bit
    IoOutByte( 0x03, COMIO + 3 );

    // FIFO control
    IoOutByte( 0xc7, COMIO + 2 );

    // Turn on DTR, RTS, and OUT2
    IoOutByte( 0x0b, COMIO + 4 );
}


void GdbPutChar( char c ) {

    IoOutByte( c, COMIO ); 
}


int GdbGetChar( void ) {

    if( IoInByte( COMIO + 5 ) & 0x01 ) {
    
        return (IoInByte( COMIO ) & 0xff);   
    }

    return 0;
}


void GdbExceptionHandler( int exception_number, void *exception_address ) {



}


