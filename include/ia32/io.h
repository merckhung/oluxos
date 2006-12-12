/*
 * Copyright (C) 2006 - 2007 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * @OLUXORG_LICENSE_HEADER_START@
 * @OLUXORG_LICENSE_HEADER_END@
 *
 */


void ia32_IoOutByte( const __u8 value, const __u16 port );
__u8 ia32_IoInByte( const __u16 port );

void ia32_IoOutWord( const __u16 value, const __u16 port );
__u16 ia32_IoInWord( const __u16 port );

void ia32_IoOutDWord( const __u32 value, const __u16 port );
__u32 ia32_IoInDWord( const __u16 port );


