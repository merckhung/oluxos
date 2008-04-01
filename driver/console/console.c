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
#include <driver/sercon.h>


static volatile u8 *VideoRamPtr = (u8 *)VIDEO_TEXT_ADDR;
static s8 buf[ CONSOLE_BUF_LEN ];
static u8 xPos = 0;
static u8 yPos = 0;


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
void TcPrint( const s8 *format, ... ) {

    s8 *p;


    // Handle String Format
    if( CbFmtPrint( buf, CONSOLE_BUF_LEN, format, (&format) + 1 ) ) {
    
        return;
    }

    
    // Output to console
    for( p = buf ; *p ; p++ ) {
    
        // Output to VGA Text Mode Screen
        TcPutchar( *p );

        // Output to Serial Console
        ScPutChar( *p );
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

    u16 i;

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
void TcCursorSet( u8 x, u8 y ) {


    u16 offset;


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
    IoOutByte( (u8)((offset >> 8) & 0xff) , CRTC_DATA );


    IoOutByte( 0x0f, CRTC_ADDR );
    IoOutByte( (u8)(offset & 0xff) , CRTC_DATA );
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
void TcPutchar( s8 c ) {

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
void TcRollUp( u8 lines ) {

    u16 i, sp, ep;


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


