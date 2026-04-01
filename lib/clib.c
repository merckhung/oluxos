/*
 * Copyright (C) 2006 - 2008 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * File: clib.c
 * Description:
 *  Kernel C library
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
//  *mem    --  Memory pointer
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
//  *dest   -- Destination memory pointer
//  *src    -- Source memory pointer
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
//  *str    -- String pointer
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
//  *dest   -- Destination string
//  *src    -- Source string
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
// CbStrCat -- Concatenate a string
//
// Input:
//  *dest   -- Destination string
//  *src    -- Source string
//  sz      -- Length of string
//
// Return:
//  Destination string
//
s8 *CbStrCat( s8 *dest, const s8 *src, s32 sz ) {

	s32 slen, dlen, i, rest;


	dlen = CbStrLen( dest ) + 1;
	rest = sz - dlen;
	if( rest < 1 ) {

		return dest;
	}


    slen = CbStrLen( src );
	if( slen > rest ) {

		slen = rest;
	}


    for( i = 0 ; i < slen ; i++ ) {
    
        if( i < slen ) {
        
            *(dest + dlen + i) = *(src + i);
        }
        else {
        
            *(dest + dlen + i) = 0;
            break;
        }
    }


    return dest;
}


//
// CbStrCmp -- Compare two strings
//
// Input:
//  *dest   -- Destination string
//  *src    -- Source string
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
// CbStrCmpL -- Legacy Compare two strings
//
// Input:
//  *dest   -- Destination string
//  *src    -- Source string
//
// Return:
//  Less than: -1, Equal to: 0, More than: 1
//
s32 CbStrCmpL( const s8 *dest, const s8 *src ) {

    s32 sum = 0;
	u32 slen, dlen, i;


    slen = CbStrLen( src );
	dlen = CbStrLen( dest );
    if( dlen < slen ) {
    
        dlen = slen;
    }


    for( i = 0 ; i < dlen ; i++ ) {
    
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
//  *buf    -- String pointer
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
//  *buf        -- Destination buffer
//  upper       -- Upper or Lower case
//  digit       -- Digit
//  pad         -- Digit of Zero Pad
//
// Return:
//  Byte just wrote
//
u32 CbBinToAsciiBuf(u32 value, s8* buf, s8 upper, u32 digit, u32 pad) {
  s8* orig = buf;
  s8 tmp[16];
  s32 i = 0;
  u32 len;
  s32 padding;

  if (value == 0) {
    tmp[i++] = '0';
  } else {
    while (value > 0) {
      u8 c = value & 0xF;
      tmp[i++] = CbBinToAscii(c, upper);
      value >>= 4;
    }
  }
  len = i;

  // Compute padding
  u32 dp = digit > pad ? digit : pad;
  padding = dp > len ? dp - len : 0;

  // Add padding characters
  for (i = 0; i < padding; i++) {
    if (digit > pad && i < (digit - pad) && pad <= len) {
      *buf++ = ' ';
    } else {
      *buf++ = '0';
    }
  }

  // Reverse string and write
  for (i = len - 1; i >= 0; i--) {
    *buf++ = tmp[i];
  }

  return (buf - orig);
}



//
// CbAsciiToBin -- Convert ASCII Byte to Binary
//
// Input:
//  value       -- ASCII Code
//
// Return:
//  Binary value
//
s8 CbAsciiToBin( s8 value ) {


    if( (value >= '0') && (value <= '9') ) {
    
        return (value - '0');
    }

    
    if( (value >= 'A') && (value <= 'F')  ) {
    
        return (value - 'A') + 10;
    }


    if( (value >= 'a') && (value <= 'f')  ) {
    
        return (value - 'a') + 10;
    }


    return 0;
}



//
// CbAsciiBufToBin  -- Convert ASCII Buffer to Binary
//
// Input:
//  *buf        -- String pointer
//
// Return:
//  Binary value
//
u32 CbAsciiBufToBin( const s8 *buf ) {

    u32 i, size, cal = 0;


    if( !buf ) {
    
        return 0;
    }


    size = CbStrLen( buf );
    for( i = 0 ; buf[ i ] ; i++ ) {

        cal += (CbAsciiToBin( buf[ i ] ) * CbPower( 16, size - i - 1 ));
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
    u8 buf[ 8 ];

    for( i = 0 ; i < 8 ; i++ ) {
    
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
s32 CbPower(s32 x, s32 y) {
  s32 sum = 1;

  while (y > 0) {
    sum *= x;
    y--;
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
//  *fmt        -- String buffer
//  *digit      -- Number of digit
//  *pad        -- Number of digit of zero pad
//  *fc         -- Format char
//
// Return:
//  Number of digit
//
// Known Bug:
//  %16.24d will become digit = 0x16, pad = 0x24, not decimal value
//
u32 CbParseFormat(const s8* fmt, u32* digit, u32* pad, s8* fc) {
  const s8* orig = fmt;
  *digit = 0;
  *pad = 0;
  *fc = 0;

  if (*fmt != '%') return 0;
  fmt++;

  // Parse pad
  if (*fmt == '0') {
      fmt++;
      while (*fmt >= '0' && *fmt <= '9') {
        *pad = (*pad * 10) + (*fmt - '0');
        fmt++;
      }
  }

  // Parse digit
  while (*fmt >= '0' && *fmt <= '9') {
    *digit = (*digit * 10) + (*fmt - '0');
    fmt++;
  }

  // Handle specific format chars
  if (*fmt == 'd' || *fmt == 'x' || *fmt == 'X' || *fmt == 'p' || *fmt == 'c' || *fmt == 's') {
    *fc = *fmt;
    fmt++;
    return (u32)(fmt - orig);
  }

  return 0;
}



//
// CbFmtPrint   -- Format print format
//
// Input:
//  *buf        -- Output buffer
//  sz          -- Size of buffer
//  *format     -- Format string
//  **args      -- Arguments
//
// Return:
//  Success : 0
//  Error   : 1
//
s32 CbFmtPrint( s8 *buf, u32 sz, const s8 *format, va_list args ) {

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

                buf += CbBinToAsciiBuf( va_arg(args, u32), buf, upper, digit, pad );
                break;


            // Character Print
            case 'c' :

                *buf = (s32)va_arg(args, s32);
                buf++;
                break;


            // String Print
            case 's' :
                {
                    s8* str_arg = va_arg(args, s8*);
                    for( ; *str_arg ; str_arg++, buf++ ) {

                        *buf = (s8)*str_arg;
                    }
                }
                break;


            // Decimal Print
            case 'd' :

                buf += CbBinToAsciiBuf( CbBinToBcd( va_arg(args, u32) ), buf, upper, digit, pad );   
                break;


            // Bad syntax
            default :
                return 1;
        }

    }


    return 0;
}


