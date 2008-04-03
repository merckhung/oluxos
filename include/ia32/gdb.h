/*
 * Copyright (C) 2006 - 2008 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * @OLUXORG_LICENSE_HEADER_START@
 * @OLUXORG_LICENSE_HEADER_END@
 *
 */



//
// Definitions
//
#define GDB_BUF_LEN     400



//
// Prototypes
//
void GdbInit( void );
void GdbPutChar( s8 c );
s8 GdbGetChar( void );
void GdbGetPacket( s8 *buf );
void GdbSendPacket( s8 *buf );
void GdbExceptionHandler( u32 exception_number, void *exception_address );



