/*
 * Copyright (C) 2006 - 2007 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * @OLUXORG_LICENSE_HEADER_START@
 * @OLUXORG_LICENSE_HEADER_END@
 *
 * string.c -- OluxOS IA32 memory routines
 *
 */
#include <types.h>
#include <clib.h>


void *memset( void *s, __u8 c, __u32 n ) {

    __u32 i;
    __u8 *p = (__u8 *)s;

    for( i = 0 ; i < n ; i++ ) {
    
        *(p + i) = c;
    } 

    return s;
}


void *memcpy( void *dest, const void *src, __u32 n ) {

    __u32 i;
    __u8 *pd = (__u8 *)dest;
    __u8 *ps = (__u8 *)src;


    for( i = 0 ; i < n ; i++ ) {
    
        *(pd + i) = *(ps + i);
    }

    return dest;
}


__s8 *strncpy( __s8 *dest, const __s8 *src, __u32 n ) {

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


__s8 strncmp( __s8 *dest, const __s8 *src, __u32 n ) {

    __u32 len, i, sum = 0;

    len = strlen( src );
    for( i = 0 ; i < n ; i++ ) {
    
        if( i < len ) {
        
            sum += *(dest + i) - *(src + i);
            if( sum ) {
            
                break;
            }
        }
        else {
        
            break;
        }
    }

    return sum; 
}


__u32 strlen( const __s8 *s ) {

    __u32 sum;

    for( sum = 0 ; *(s + sum) ; sum++ );

    return sum;
}


__s8 itoa( const __s8 value, const __s8 upper ) {

    if( value > 15 ) {

        return '0';
    }

    if( value > 9 ) {

        if( upper ) {

            return (value - 10) + 'A';
        }
        else {
        
            return (value - 10) + 'a';
        }
    }

    return value + '0';
}


__s32 pow( __s32 x, __s32 y ) {

    __s32 sum = 0;

    if( !y ) {
    
        return 1;
    } 
    else {
    
        sum += x * pow( x, y - 1 );
    }

    return sum;
}


__s32 itobcd( __s32 value ) {

    __s8 len = sizeof( __s32 ) * 2;
    __s8 buf[ len ];
    __s32 i, result = 0;

    for( i = 0 ; i < len ; i++ ) {
    
        if( value ) {

            buf[ i ] = value % 10;
            value /= 10;
        }
        else {
        
            break;
        }
    }

    for( ; i ; i-- ) {
    
        result += pow( 16, i - 1 ) * buf[ i - 1 ];
    }

    return result;
}


