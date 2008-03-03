/*
 * Copyright (C) 2006 - 2007 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * @OLUXORG_LICENSE_HEADER_START@
 * @OLUXORG_LICENSE_HEADER_END@
 *
 */


#define COMA    0x3f8
#define COMB    0x2f8
#define COMIO   COMA


void GdbInit( void );
void GdbPutChar( char c );
int GdbGetChar( void );
void GdbExceptionHandler( int exception_number, void *exception_address );


