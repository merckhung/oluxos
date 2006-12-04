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
#include <ia32/io.h>
#include <ia32/console.h>


static volatile unsigned char *VideoRamPtr = (unsigned char *)VIDEO_TEXT_ADDR;
static unsigned char xPos = 0;
static unsigned char yPos = 0;


void ia32_TcPrint( const char *format, ... ) {

    int i;
    char **arg = (char **) &format;
    arg++;

    for( ; *format ; format++ ) {
    
        if( *format == '%' ) {
        
            switch( *(format + 1) ) {
            
                case 'U' :
                case 'u' :
                    ia32_TcPutChar( itoa( *arg ) );
                    break;

                case '8' :
                    if( *(format + 2) == 'U' || *(format + 2) == 'u' ) {
                    
                        char res[ 8 ];
                        litoa( res, *arg );

                        for( i = 7 ; i >= 0 ; i-- ) {
                        
                            ia32_TcPutChar( res[ i ] ); 
                        }
                        format++;
                    }
                    break;

                case '4' :
                    if( *(format + 2) == 'U' || *(format + 2) == 'u' ) {
                    
                        char res[ 4 ];
                        witoa( res, *arg );

                        for( i = 3 ; i >= 0 ; i-- ) {
                        
                            ia32_TcPutChar( res[ i ] ); 
                        }
                        format++;
                    }
                    break;

                default :
                    break;
            }

            format++;
            arg++;
        }
        else {

            ia32_TcPutChar( *format );
        }
    }
}


void ia32_TcClear( void ) {

    int i;

    for( i = 0 ; i < (COLUMN * 2 * LINE) ; i++ ) {
    
        *(VideoRamPtr + i) = ( i % 2 ) ? 0x07 : 0x00; 
    }
}


void ia32_TcCursorSet( unsigned char x, unsigned char y ) {


    if( x >= COLUMN ) {
    
        x = COLUMN - 1;
    }

    if( y >= LINE ) {
    
        y = LINE - 1;
    }

    xPos = x;
    yPos = y;

    ia32_PtOutB( 0x0e, CRTC_ADDR );
    ia32_PtOutB( 0x99, CRTC_DATA );
    ia32_PtOutB( 0x0f, CRTC_ADDR );
    ia32_PtOutB( 0x76, CRTC_DATA );
    ia32_PtInB( 0x0e );
}


void ia32_TcPutChar( unsigned char Character ) {


    if( Character == '\n' ) {
    
        xPos = 0;
        yPos++;
        if( yPos >= LINE ) {
    
            ia32_TcRollUp( 1 );
            yPos = LINE - 1;
        }

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
}


void ia32_TcRollUp( unsigned char Lines ) {

    int i, sp, ep;


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



