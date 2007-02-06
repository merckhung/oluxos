/*
 * Copyright (C) 2006 -  2007 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * @OLUXORG_LICENSE_HEADER_START@
 * @OLUXORG_LICENSE_HEADER_END@
 *
 * console.c -- OluxOS IA32 text mode console routines
 *
 */
#include <types.h>
#include <clib.h>
#include <ia32/io.h>
#include <driver/console.h>


static volatile __u8 *VideoRamPtr = (__u8 *)VIDEO_TEXT_ADDR;
static __u8 xPos = 0;
static __u8 yPos = 0;


//
// TcPrint
//
// Input:
//  format  : string to print
//  ...     : arguments to use
//
// Return:
//  None
//
// Description:
//  Print string on console just like standard C printf() routine
//
void TcPrint( const __s8 *format, ... ) {

    __s32 digit;
    __s8 **arg = (__s8 **) (&format) + 1;

    for( ; *format ; format++ ) {

        if( *format == '%' ) {

            __s8 upper = 0, had = 0, specify = 1;
            __s8 tmp;

            // Skip '%'
            format++;

            // Calculate and convert digit
            format += TcCalDigit( format, &digit );

            // Auto sizing
            if( !digit ) {

                digit = sizeof( __u32 );
                specify = 0;
            }

            switch( *format ) {


                // Hex Print
                case 'X' :
                    upper = 1;
                case 'x' :
                    for( digit-- ; digit >= 0 ; digit-- ) {

                        tmp = itoa( (__s8)( (((__u32)(*arg)) >> (digit * 4)) & 0xf), upper );
                        if( specify ) {
                        
                            TcPutchar( tmp );
                        }
                        else {
                        
                            if( (tmp != '0') || had ) {

                                had = 1;
                                TcPutchar( tmp );
                            }
                        }
                    }
                    break;


                // ASCII Print
                case 'c' :

                    TcPutchar( (__u32)*arg );
                    break;

                
                // String Print
                case 's' :
                    for( ; **arg ; (*arg)++ ) {
                
                        TcPutchar( **arg );
                    }
                    break;


                // Decimal Print
                case 'd' :
                    digit = 8;
                    for( digit-- ; digit >= 0 ; digit-- ) {

                        tmp = itoa( (__s8)( (((__u32)(itobcd((__s32)*arg))) >> (digit * 4)) & 0xf), upper );
                        if( (tmp != '0') || had ) {

                            had = 1; 
                            TcPutchar( tmp );
                        }
                    }
                    break;


                // Cannot parse syntax
                default :
                    continue;
            }

            arg++;
        }
        else {

            TcPutchar( *format );
        }
    }
}


//
// TcCalDigit
//
// Input:
//  p       : String buffer
//  digit   : number of digit output
//
// Return:
//  None
//
// Description:
//  Calculate number of digit
//
#define     MAX_FORMAT_DIGIT    2
__s32 TcCalDigit( const __s8 *p, __s32 *digit ) {

    __s32 i, j;
    __s8 buf[ MAX_FORMAT_DIGIT ];

    for( i = 0 ; i < MAX_FORMAT_DIGIT ; i++, p++ ) {
    
        if( (*p >= '0') && (*p <= '9') ) {

            buf[ i ] = *p - '0';
        }
        else {
        
            break;
        }
    }

    *digit = 0;
    for( j = 0 ; j < i ; j++ ) {
    
        *digit += pow( 10, i - j - 1 ) * buf[ j ];
    }

    return i;
}


//
// TcClear
//
// Input:
//  None
//
// Return:
//  None
//
// Description:
//  Clear console screen and reset cursor to (0, 0)
//
void TcClear( void ) {

    __u16 i;

    for( i = 0 ; i < (COLUMN * 2 * LINE) ; i++ ) {
    
        *(VideoRamPtr + i) = ( i % 2 ) ? 0x07 : 0x00; 
    }

    TcCursorSet( 0, 0 );
}


//
// TcCursorSet
//
// Input:
//  x       : Console column number (0 - 79)
//  y       : Console line number   (0 - 24)
//
// Return:
//  None
//
// Description:
//  Set console cursor position
//
void TcCursorSet( __u8 x, __u8 y ) {


    __u16 offset;


    if( x >= COLUMN ) {
    
        x = COLUMN - 1;
    }

    if( y >= LINE ) {
    
        y = LINE - 1;
    }

    xPos = x;
    yPos = y;

    offset = (yPos * COLUMN) + xPos;

    IoOutByte( 0x0e, CRTC_ADDR );
    IoOutByte( (__u8)((offset >> 8) & 0xff) , CRTC_DATA );


    IoOutByte( 0x0f, CRTC_ADDR );
    IoOutByte( (__u8)(offset & 0xff) , CRTC_DATA );
}


//
// TcPutchar
//
// Input:
//  c       : Character to put on screen
//
// Return:
//  None
//
// Description:
//  Put one char on screen
//
void TcPutchar( __s8 c ) {

    if( c == '\n' ) {
    
        xPos = 0;
        yPos++;
        if( yPos >= LINE ) {
    
            TcRollUp( 1 );
            yPos = LINE - 1;
        }

        TcCursorSet( xPos, yPos );
        return;
    }

    *(VideoRamPtr + (yPos * COLUMN * 2) + (xPos * 2)) = c;

    xPos++;
    if( xPos >= COLUMN ) {
    
        xPos = 0;
        yPos++;
        if( yPos >= LINE ) {
    
            TcRollUp( 1 );
            yPos = LINE - 1;
        }
    }

    TcCursorSet( xPos, yPos );
}


//
// TcRollUp
//
// Input:
//  line    : How many lines to roll up
//
// Return:
//  None
//
// Description:
//  Roll up screen
//
void TcRollUp( __u8 lines ) {

    __u16 i, sp, ep;


    if( lines >= LINE ) {
    
        TcClear();
        return;
    }
    
    
    if( lines == 0 ) {
    
        return;
    }


    sp = lines * COLUMN * 2;
    ep = (LINE - lines) * COLUMN * 2;
    for( i = 0 ; i < (COLUMN * 2 * LINE) ; i++ ) {
    
        if( i >= ep ) {
        
            *(VideoRamPtr + i) = ( i % 2 ) ? 0x07 : 0x00;
        } 
        else {
        
            *(VideoRamPtr + i) = *(VideoRamPtr + sp);
            sp++;
        }
    }
}


