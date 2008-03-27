/*
 * Copyright (C) 2006 - 2008 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * @OLUXORG_LICENSE_HEADER_START@
 * @OLUXORG_LICENSE_HEADER_END@
 *
 * clib.c -- OluxOS General C Routines
 *
 */
#include <types.h>
#include <clib.h>



////////////////////////////////////////////////////////////////////////////////
// Memory/String Routines                                                     //
////////////////////////////////////////////////////////////////////////////////


//
// CbMemSet -- Fill memory with constant byte
//
// Input:
//  mem     --  Memory pointer
//  ch      --  Constant byte to write
//  sz      --  Size of Memory buffer
//
// Return:
//  Original memory pointer
//
void *CbMemSet( void *mem, u8 ch, u32 sz ) {

    u8 *p = (u8 *)mem;

    for( ; sz-- ; p++ ) {
       
        *p = ch;
    }

    return mem;
}


//
// CbMemCpy -- Copy memory area
//
// Input:
//  dest    -- Destination memory pointer
//  src     -- Source memory pointer
//  sz      -- Size of area to copy
//
// Return:
//  Destination memory pointer
//
void *CbMemCpy( void *dest, const void *src, u32 sz ) {

    u8 *d = (u8 *)dest;
    u8 *s = (u8 *)src;

    for( ; sz-- ; d++, s++ ) {
    
        *d = *s;
    }

    return dest;
}


//
// CbStrLen -- Calculate the length of a string
//
// Input:
//  str     -- String pointer
//
// Return:
//  Length of string
//
u32 CbStrLen( const s8 *str ) {

    u32 i;

    for( i = 0 ; str[ i ] ; i++ );

    return i;
}


//
// CbStrCpy -- Copy a string
//
// Input:
//  dest    -- Destination string
//  src     -- Source string
//  sz      -- Length of string
//
// Return:
//  Destination string
//
s8 *CbStrCpy( s8 *dest, const s8 *src, u32 sz ) {

    u32 len, i;

    len = CbStrLen( src );
    for( i = 0 ; i < sz ; i++ ) {
    
        if( i < len ) {
        
            *(dest + i) = *(src + i);
        }
        else {
        
            *(dest + i) = 0;
            break;
        }
    }

    return dest;
}


//
// CbStrCmp -- Compare two strings
//
// Input:
//  dest    -- Destination string
//  src     -- Source string
//  sz      -- Length of string
//
// Return:
//  Less than: -1, Equal to: 0, More than: 1
//
s32 CbStrCmp( const s8 *dest, const s8 *src, u32 sz ) {

    s32 sum = 0;
    u32 i, len;


    len = CbStrLen( src );
    if( len > sz ) {
    
        len = sz;
    }


    for( i = 0 ; i < len ; i++ ) {
    
        sum += (dest[ i ] - src[ i ]);
        if( sum ) {
            
            break;
        }
    }


    return sum;
}


//
// CbIndex  -- Locate character in string
//
// Input:
//  buf     -- String pointer
//  ch      -- Character to locate
//
// Return:
//  A pointer to the matched character ot NULL
//
s8 *CbIndex( const s8 *buf, const s8 ch ) {

    u32 i;
    s8 *p = (s8 *)buf;

    for( i = 0 ; p[ i ] ; i++ ) {
    
        if( p[ i ] == ch ) {
        
            return (p + i);
        }
    }

    return NULL;
}



////////////////////////////////////////////////////////////////////////////////
// ASCII Routines                                                             //
////////////////////////////////////////////////////////////////////////////////


//
// CbBinToAscii -- Convert Binary to ASCII
//
// Input:
//  value       -- Byte to do convert
//  upper       -- Use upper case or not
//
// Return:
//  ASCII code
//
s8 CbBinToAscii( const s8 value, const s8 upper ) {

    if( value > 15 ) {

        return '0';
    }

    if( value > 9 ) {

        if( upper == UPPERCASE ) {

            return (value - 10) + 'A';
        }
        else {
        
            return (value - 10) + 'a';
        }
    }

    return value + '0';
}


//
// CbAsciiToBin -- Convert ASCII to Binary
//
// Input:
//  buf         -- String pointer
//
// Return:
//  Binary value
//
u32 CbAsciiToBin( const s8 *buf ) {

    u32 i, j, size, cal = 0;
    s8 *hex = "0123456789abcdef";
    s8 *hex2 = "0123456789ABCDEF";


    if( !buf ) {
    
        return 0;
    }


    size = CbStrLen( buf );
    for( i = 0 ; buf[ i ] ; i++ ) {

        for( j = 0 ; j < 16 ; j++ ) {

            if( (hex[ j ] == buf[ i ]) || (hex2[ j ] == buf[ i ]) ) {

                break;
            }
        }

        cal += (j * CbPower( 16, size - i - 1 ));
    }


    return cal;
}


//
// CbBinToBcd   -- Convert Binary to BCD
//
// Input:
//  value       -- Binary value to do convert
//
// Return:
//  BCD value
//
u32 CbBinToBcd( u32 value ) {

    u32 i, rs = 0;
    u8 len = sizeof( u32 ) * 2;
    u8 buf[ len ];

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
    
        rs += (CbPower( 16, i - 1 ) * buf[ i - 1 ]);
    }

    return rs;
}


//
// CbBcdToBin   -- Convert BCD to Binary
//
u32 CbBcdToBin( u32 dec, s8 *buf, u32 len ) {

    u32 i, j;
    s8 forward[ BUF_LEN ], reverse[ BUF_LEN ];


    for( i = 0 ; i < BUF_LEN ; i++ ) {

        reverse[ i ] = dec % 2 ? '1' : '0' ;
        dec /= 2;
    }


    reverse[ BUF_LEN - 1 ] = 0;


    for( i = 0, j = (BUF_LEN - 2) ; i < (BUF_LEN - 1) ; i++, j-- ) {

        forward[ i ] = reverse[ j ];
    }


    forward[ i ] = 0;
    CbStrCpy( buf, (forward + ( (BUF_LEN - 1) - len)), len );


    return 0;
}



////////////////////////////////////////////////////////////////////////////////
// Mathematics Routines                                                       //
////////////////////////////////////////////////////////////////////////////////


//
// CbPower      -- Power function
//
// Input:
//  x           -- Base number
//  y           -- Exponent
//
// Return:
//  The value x raised to the power of y
//
s32 CbPower( s32 x, s32 y ) {

    s32 sum = 0;

    if( !y ) {
    
        return 1;
    } 
    else {
    
        sum += x * CbPower( x, y - 1 );
    }

    return sum;
}


