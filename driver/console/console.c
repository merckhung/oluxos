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
#include <ia32/types.h>
#include <ia32/io.h>
#include <driver/console.h>
#include <string.h>


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

    __s8 digit;
    __s8 **arg = (__s8 **) (&format) + 1;

    for( ; *format ; format++ ) {
    
        if( *format == '%' ) {

            format++;
            digit = 0;
            if( (*format >= '0') && (*format <= '8') ) {
            
                digit = *format - '0';
                format++;
            }

            switch( *format ) {

                case 'X' :
                case 'x' :

                    // Auto sizing
                    if( !digit ) {
                    
                        digit = sizeof( **arg ) * 8;
                    }
                    for( digit-- ; digit >= 0 ; digit-- ) {
                    
                        TcPutChar( itoa( (__s8)( (((__u32)(*arg)) >> (digit * 4)) & 0xf) ) );
                    }
                    break;

                default :
                    continue;
            }

            arg++;
        }
        else {

            TcPutChar( *format );
        }
    }
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
// TcPutChar
//
// Input:
//  c       : Character to put on screen
//
// Return:
//  None
//
// Description:
//  Put one character on screen
//
void TcPutChar( __s8 c ) {


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


