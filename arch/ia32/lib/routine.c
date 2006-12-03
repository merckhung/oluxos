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


char itoa( const unsigned char value ) {

    if( value > 15 ) {
    
        return '0';
    }

    if( value > 9 ) {
    
        return (value - 10) + 'A';
    }

    return value + '0';
}


void litoa( char *result, const unsigned long int value ) {

    int i;

    for( i = 0 ; i < 8 ; i++ ) {
    
        result[ i ] = itoa( (unsigned char)((value >> (i * 4)) & 0x0000000f) );
    }
}


void witoa( char *result, const unsigned int value ) {

    int i;

    for( i = 0 ; i < 4 ; i++ ) {
    
        result[ i ] = itoa( (unsigned char)((value >> (i * 4)) & 0x000f) );
    }
}


