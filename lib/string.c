/*
 * Copyright (C) 2007 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * @OLUXORG_LICENSE_HEADER_START@
 * @OLUXORG_LICENSE_HEADER_END@
 *
 * string.c -- OluxOS IA32 memory routines
 *
 */
#include <ia32/types.h>
#include <string.h>


void *memcpy( __u8 *dest, const __u8 *src, __u32 n ) {

    __u32 i;


    for( i = 0 ; i < n ; i++ ) {
    
        *(dest + i) = *(src + i);
    }

    return dest;
}


__u8 *strncpy( __u8 *dest, const __u8 *src, __u32 n ) {

    __u32 len, i;


    len = strlen( src );
    for( i = 0 ; i < n ; i++ ) {
    
        if( i < len ) {
        
            *(dest + i) = *(src + i);
        }
        else {
        
            *(dest + i) = 0;
        }
    }

    return dest;
}


__u32 strlen( const __u8 *s ) {

    __u32 sum;

    for( sum = 0 ; *(s + sum) ; sum++ );

    return sum;
}


__s8 itoa( const __u8 value ) {

    if( value > 15 ) {
    
        return '0';
    }

    if( value > 9 ) {
    
        return (value - 10) + 'A';
    }

    return value + '0';
}



