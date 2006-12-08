/*
 * Copyright (C) 2007 Olux Organization All rights reserved.
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
#include <ia32/console.h>
#include <string.h>


static volatile unsigned char *VideoRamPtr = (unsigned char *)VIDEO_TEXT_ADDR;
static __u8 xPos = 0;
static __u8 yPos = 0;


void ia32_TcPrint( const __s8 *format, ... ) {

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
                    
                        ia32_TcPutChar( itoa( (__s8)( (((__u32)(*arg)) >> (digit * 4)) & 0xf) ) );
                    }
                    break;

                default :
                    continue;
            }

            arg++;
        }
        else {

            ia32_TcPutChar( *format );
        }
    }
}


void ia32_TcClear( void ) {

    __u16 i;

    for( i = 0 ; i < (COLUMN * 2 * LINE) ; i++ ) {
    
        *(VideoRamPtr + i) = ( i % 2 ) ? 0x07 : 0x00; 
    }

    ia32_TcCursorSet( 0, 0 );
}


void ia32_TcCursorSet( __u8 x, __u8 y ) {


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

    ia32_IoOutByte( 0x0e, CRTC_ADDR );
    ia32_IoOutByte( (__u8)((offset >> 8) & 0xff) , CRTC_DATA );


    ia32_IoOutByte( 0x0f, CRTC_ADDR );
    ia32_IoOutByte( (__u8)(offset & 0xff) , CRTC_DATA );
}


void ia32_TcPutChar( __s8 Character ) {


    if( Character == '\n' ) {
    
        xPos = 0;
        yPos++;
        if( yPos >= LINE ) {
    
            ia32_TcRollUp( 1 );
            yPos = LINE - 1;
        }

        ia32_TcCursorSet( xPos, yPos );
        return;
    }

    *(VideoRamPtr + (yPos * COLUMN * 2) + (xPos * 2)) = Character;

    xPos++;
    if( xPos >= COLUMN ) {
    
        xPos = 0;
        yPos++;
        if( yPos >= LINE ) {
    
            ia32_TcRollUp( 1 );
            yPos = LINE - 1;
        }
    }

    ia32_TcCursorSet( xPos, yPos );
}


void ia32_TcRollUp( __u8 Lines ) {

    __u16 i, sp, ep;


    if( Lines >= LINE ) {
    
        ia32_TcClear();
        return;
    }
    
    
    if( Lines == 0 ) {
    
        return;
    }


    sp = Lines * COLUMN * 2;
    ep = (LINE - Lines) * COLUMN * 2;
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



