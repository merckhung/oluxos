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
//  upper       -- Upper or Lower case
//
// Return:
//  ASCII code
//
s8 CbBinToAscii( s8 value, s8 upper ) {

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
// CbBinToAsciiBuf  --  Convert a Binary value to Ascii code then push into buffer
//
// Input:
//  value       -- Binary value to do convert
//  buf         -- Destination buffer
//  upper       -- Upper or Lower case
//  digit       -- Digit
//  pad         -- Digit of Zero Pad
//
// Return:
//  Byte just wrote
//
u32 CbBinToAsciiBuf( u32 value, s8 *buf, s8 upper, u32 digit, u32 pad ) {

    s32 hexdig = sizeof( value ) * 2;
    s8 *orig = buf, tmp[ hexdig + 1 ], *p, c;
    u32 len, i, dp;


    // Initialization
    p = tmp;
    CbMemSet( tmp, 0, hexdig + 1 );


    // Convert each half byte to ascii
    for( hexdig--, i = 0 ; hexdig >= 0 ; hexdig-- ) {
    
        c = (value >> (hexdig * 4)) & 0xF;
        if( !i && !c ) {
        
            continue;
        }

        *p = CbBinToAscii( c, upper );
        p++;
        i = 1;
    }


    // Handle digit, pad, and push into buffer
    len = CbStrLen( tmp );


    dp = pad;
    if( digit > pad ) {
    
        dp = digit;    
    }


    i = dp - len;
    if( len >= dp ) {
    
        i = 0;
    }

    
    // Padding
    for( ; i-- ; buf++ ) {
    
        c = '0';
        if( digit > pad ) {
        
            if( i >= (pad - len)  ) {
            
                c = ' ';
            }
        }


        *buf = c;
    }


    // Copy ASCII
    for( i = 0 ; i < len ; i++, buf++ ) {
    
        *buf = tmp[ i ];
    }


    return (buf - orig);
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
    s8 *hex1 = "0123456789abcdef";
    s8 *hex2 = "0123456789ABCDEF";


    if( !buf ) {
    
        return 0;
    }


    size = CbStrLen( buf );
    for( i = 0 ; buf[ i ] ; i++ ) {

        for( j = 0 ; j < 16 ; j++ ) {

            if( (hex1[ j ] == buf[ i ]) || (hex2[ j ] == buf[ i ]) ) {

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
// Input:
//  value       -- BCD value to do convert
//
// Return:
//  Binary value
//
u32 CbBcdToBin( u32 value ) {

    u32 i, j, rs;

    for( i = 0, j = 0, rs = 0 ; ; i += 4, j++ ) {

        if( !(value >> i) ) {
        
            break;
        }

        rs += (CbPower( 10, j ) * ((value >> i) & 0xF));
    }

    return rs;
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



////////////////////////////////////////////////////////////////////////////////
// General Routines                                                           //
////////////////////////////////////////////////////////////////////////////////


//
// CbParseFormat-- Parsing format digit
//
// Input:
//  buf         -- String buffer
//  digit       -- Number of digit
//  pad         -- Number of digit of zero pad
//  fc          -- Format char
//
// Return:
//  Number of digit
//
// Known Bug:
//  %16.24d will become digit = 0x16, pad = 0x24, not decimal value
//
u32 CbParseFormat( const s8 *fmt, u32 *digit, u32 *pad, s8 *fc ) {

    s8 buf[ FMT_MAX_DIG + 1 ];
    u8 i, j, p;
    s8 value[ 2 ];
    s8 *orig = (s8 *)fmt;


    // Initialization
    *digit = 0;
    *pad = 0;
    *fc = 0;
    p = 0;


    // Check '%'
    if( *fmt != '%' ) {
    
        return 0;
    }


    // Skip '%'
    fmt++;


    // Parsing format
    for( j = 0 ; j < 2 ; j++ ) {


        // Clear buffer
        CbMemSet( buf, 0, FMT_MAX_DIG + 1 );

        
        for( i = 0 ; i < FMT_MAX_DIG ; fmt++ ) {

            // Check '.' character
            if( *fmt == '.' ) {

                if( p == 1 ) {
                
                    return 0;
                }

                p = 1;
                fmt++;
                break;
            }


            // Check if it's ASCII
            if( (*fmt < '0') || (*fmt > '9') ) {
        
                break;
            }


            // Push into buffer
            buf[ i ] = *fmt;
            i++;
        }


        // Convert buffer to binary
        value[ j ] = CbAsciiToBin( buf );        
    }


    // If it's a valid format char, move to next
    switch( *fmt ) {
    
        case 'd':
        case 'x':
        case 'X':
        case 'p':
            goto norexit;

        case 'c':
        case 's':
            goto nodexit;

        default:
            return 0;
    }


norexit:


    // Write result
    *digit = value[ 0 ];
    *pad = value[ 1 ];


nodexit:

    *fc = *fmt;
    fmt++;

    // Return offset 
    return (fmt - orig);
}



//
// CbFmtPrint   -- Format print format
//
// Input:
//  buf         -- Output buffer
//  sz          -- Size of buffer
//  format      -- Format string
//  ...         -- Arguments
//
// Return:
//  Success : 0
//  Error   : 1
//
s32 CbFmtPrint( s8 *buf, u32 sz, const s8 *format, ... ) {

    s8 **args = (s8 **) (&format) + 1;
    s8 *obuf = buf;
    s8 fc, upper;
    u32 digit, pad;


    // Clear memory first
    CbMemSet( buf, 0, sz );


    // Manipulate String
    for( ; *format ; ) {


        // Check buffer overflow
        if( (buf - obuf) > sz ) {
        
            return 1;
        }


        // Direct put char if it's not '%' char
        if( *format != '%' ) {
        
            *buf = *format;
            buf++;
            format++;
            continue;
        }


        // Handle '%%'
        if( *(format + 1) == '%' ) {
        
            *buf = *format;
            buf++;
            format += 2;
            continue;
        }


        // Get digit, pad, offset from format string
        // Then offset to the end of format string
        format += CbParseFormat( format, &digit, &pad, &fc );


        // Initialization
        upper = LOWERCASE;


        // Print Format
        switch( fc ) {

                // Hexadecimal Print
                case 'X' :

                    upper = UPPERCASE;

                case 'x' :

                    buf += CbBinToAsciiBuf( (u32)*args, buf, upper, digit, pad );
                    break;


                // Character Print
                case 'c' :

                    *buf = (s32)*args;
                    buf++;
                    break;


                // String Print
                case 's' :

                    for( ; **args ; (*args)++, buf++ ) {

                        *buf = (s8)**args;
                    }
                    break;


                // Decimal Print
                case 'd' :

                    buf += CbBinToAsciiBuf( CbBinToBcd( (u32)*args ), buf, upper, digit, pad );   
                    break;


                // Bad syntax
                default :
                    return 1;
            }

            
            // Move to next argsument
            args++;
    }


    return 0;
}



