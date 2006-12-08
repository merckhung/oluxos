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


