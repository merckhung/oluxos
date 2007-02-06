/*
 * Copyright (C) 2006 - 2007 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * @OLUXORG_LICENSE_HEADER_START@
 * @OLUXORG_LICENSE_HEADER_END@
 *
 */


void *memset( void *s, __u8 c, __u32 n );
void *memcpy( void *dest, const void *src, __u32 n );
__s8 *strncpy( __s8 *dest, const __s8 *src, __u32 n );
__s8 strncmp( __s8 *dest, const __s8 *src, __u32 n );
__u32 strlen( const __s8 *s );
__s8 itoa( const __s8 value, const __s8 upper );
__s32 pow( __s32 x, __s32 y );
__s32 itobcd( __s32 value );


