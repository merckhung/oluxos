/*
 * Copyright (C) 2006 - 2008 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * File: serial.h
 * Description:
 * 	None
 *
 */


#define COMA    0x3f8
#define COMB    0x2f8
#define COMIO   COMA


void SrInit( void );
void SrInitInterrupt( void );
void SrIntHandler( u8 IrqNum );
void SrPutChar( s8 c );
s8 SrGetChar( void );


