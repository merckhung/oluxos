/*
 * Copyright (C) 2006 - 2007 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * @OLUXORG_LICENSE_HEADER_START@
 * @OLUXORG_LICENSE_HEADER_END@
 *
 */


void IoOutByte( const u8 value, const u16 port );
u8 IoInByte( const u16 port );

void IoOutWord( const u16 value, const u16 port );
u16 IoInWord( const u16 port );

void IoOutDWord( const u32 value, const u16 port );
u32 IoInDWord( const u16 port );


