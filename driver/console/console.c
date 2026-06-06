/*
 * Copyright (C) 2006 -  2007 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * File: console.c
 * Description:
 *	OluxOS IA32 text mode console routines
 *
 */
#include <types.h>
#include <clib.h>
#include <ia32/io.h>
#include <driver/console.h>


static volatile uint8_t *VideoRamPtr = (volatile uint8_t *)VIDEO_TEXT_ADDR;
static int8_t buf[ CONSOLE_BUF_LEN ];
static uint8_t xPos = 0;
static uint8_t yPos = 0;


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
void TcPrint( const int8_t *format, ... ) {

    int8_t *p;
    va_list args;
    va_start(args, format);

    // Handle String Format
    if( CbFmtPrint( buf, CONSOLE_BUF_LEN, format, args ) ) {

        va_end(args);
        return;
    }
    va_end(args);

    // Output to console
    for( p = buf ; *p ; p++ ) {
    
        // Output to VGA Text Mode Screen
        TcPutChar( *p );
        SrPutChar( *p );
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

    uint16_t i;

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
void TcCursorSet( uint8_t x, uint8_t y ) {


    uint16_t offset;


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
    IoOutByte( (uint8_t)((offset >> 8) & 0xff) , CRTC_DATA );


    IoOutByte( 0x0f, CRTC_ADDR );
    IoOutByte( (uint8_t)(offset & 0xff) , CRTC_DATA );
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
//  Put one char on screen
//
void TcPutChar( int8_t c ) {

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
void TcRollUp( uint8_t lines ) {

    uint16_t i, sp, ep;


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


