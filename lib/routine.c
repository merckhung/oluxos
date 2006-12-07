/*
 * Copyright (C) 2007 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * @OLUXORG_LICENSE_HEADER_START@
 * @OLUXORG_LICENSE_HEADER_END@
 *
 * routine.c -- OluxOS IA32 generic routines
 *
 */
#include <ia32/types.h>


char itoa( const __u8 value ) {

    if( value > 15 ) {
    
        return '0';
    }

    if( value > 9 ) {
    
        return (value - 10) + 'A';
    }

    return value + '0';
}


void litoa( __s8 *result, const __u32 value ) {

    __u8 i;

    for( i = 0 ; i < 8 ; i++ ) {
    
        result[ i ] = itoa( (unsigned char)((value >> (i * 4)) & 0x0000000f) );
    }
}


void witoa( __s8 *result, const __u16 value ) {

    __u8 i;

    for( i = 0 ; i < 4 ; i++ ) {
    
        result[ i ] = itoa( (unsigned char)((value >> (i * 4)) & 0x000f) );
    }
}



