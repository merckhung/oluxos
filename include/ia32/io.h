/*
 * Copyright (C) 2006 - 2007 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * @OLUXORG_LICENSE_HEADER_START@
 * @OLUXORG_LICENSE_HEADER_END@
 *
 */


void IoOutByte( const __u8 value, const __u16 port );
__u8 IoInByte( const __u16 port );

void IoOutWord( const __u16 value, const __u16 port );
__u16 IoInWord( const __u16 port );

void IoOutDWord( const __u32 value, const __u16 port );
__u32 IoInDWord( const __u16 port );


