/*
 * Copyright (C) 2008 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * File: libcomm.c
 * Description:
 *  None
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <otypes.h>
#include <olux.h>



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



u32 CbAsciiBufToBin( const s8 *buf ) {

    u32 i, size, cal = 0;


    if( !buf ) {

        return 0;
    }


    size = strlen( buf );
    for( i = 0 ; buf[ i ] ; i++ ) {

        cal += (CbAsciiToBin( buf[ i ] ) * CbPower( 16, size - i - 1 ));
    }


    return cal;
}



bool ParseOneParameter( s8 *buf, u32 *first ) {


    // Check length
    if( strlen( buf ) > 10 ) {

        return FALSE;
    }


    // Check hex digits
    if( !((*buf == '0') && (*(buf + 1) == 'x')) ) {
    
        return FALSE;
    } 


    *first = CbAsciiBufToBin( buf + 2 );
    return TRUE;
}



bool ParseTwoParameters( s8 *buf, u32 *first, u32 *second ) {

    s8 *sec;


    // Check length
    if( strlen( buf ) > 21 ) {

        return FALSE;
    }


    // Check hex digits
    if( !((*buf == '0') && (*(buf + 1) == 'x')) ) {
    
        return FALSE;
    } 


    // Search '/'
    sec = index( buf, '/' );
    if( !sec ) {
    
        return FALSE;
    }


    // Separate string into first and second
    *sec = 0;
    sec++;


    // Check hex digits
    if( !((*sec == '0') && (*(sec + 1) == 'x')) ) {
    
        return FALSE;
    } 


    // Convert ASCII to Binary
    *first = CbAsciiBufToBin( buf + 2 );
    *second = CbAsciiBufToBin( sec + 2 );


    return TRUE;
}



s8 ConvertDWordToByte( u32 *Data, u32 Offset ) {

    u32 tmp, off, bs;

    
    off = Offset / 4;
    bs = Offset % 4;
    if( bs ) {

        tmp = *(Data + off);
        tmp = ((tmp >> (bs * 8)) & 0xFF);
        return tmp;
    }

    tmp = ((*(Data + off)) & 0xFF);
    return tmp;
}



void DumpData( u32 *Data, u32 Length, u32 BaseAddr ) {

    u32 i, j;
    u32 c;


    printf( "\n\n== Dump Memory Start ==\n\n" );
    printf( " Address | 00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F|   ASCII DATA   \n" );
    printf( "---------------------------------------------------------------------------\n" );


    for( i = 0 ; i <= Length ; i++ ) {


        if( !(i % 16) ) {


            if( (i > 15) ) {

                for( j = i - 16 ; j < i ; j++ ) {

                    c = ConvertDWordToByte( Data, j );
                    if( ((c >= '!') && (c <= '~')) ) {

                        printf( "%c", c );
                    }
                    else {

                        printf( "." );
                    }
                }
            }


            if( i ) {

                printf( "\n" );
            }


            if( i == Length ) {

                break;
            }


            printf( "%8.8X : ", i + BaseAddr );
        }


        printf( "%2.2X ", ConvertDWordToByte( Data, i ) & 0xFF );
    }


    printf( "\n---------------------------------------------------------------------------\n" );
    printf( "\n== Dump Memory End ==\n\n" );
}



void DisplayInBits( u32 value ) {


    printf( "\n===============================================================================================\n" );
    printf( "31 30 29 28 27 26 25 24|23 22 21 20 19 18 17 16|15 14 13 12 11 10 09 08|07 06 05 04 03 02 01 00\n" );
    printf( "-----------------------------------------------------------------------------------------------\n" );
    printf( " %d  %d  %d  %d  %d  %d  %d  %d| %d  %d  %d  %d  %d  %d  %d  %d| %d  %d  %d  %d  %d  %d  %d  %d| %d  %d  %d  %d  %d  %d  %d  %d\n"
        , OLUX_GET_BIT( value, 31 )
        , OLUX_GET_BIT( value, 30 )

        , OLUX_GET_BIT( value, 29 )
        , OLUX_GET_BIT( value, 28 )
        , OLUX_GET_BIT( value, 27 )
        , OLUX_GET_BIT( value, 26 )
        , OLUX_GET_BIT( value, 25 )
        , OLUX_GET_BIT( value, 24 )
        , OLUX_GET_BIT( value, 23 )
        , OLUX_GET_BIT( value, 22 )
        , OLUX_GET_BIT( value, 21 )
        , OLUX_GET_BIT( value, 20 )

        , OLUX_GET_BIT( value, 19 )
        , OLUX_GET_BIT( value, 18 )
        , OLUX_GET_BIT( value, 17 )
        , OLUX_GET_BIT( value, 16 )
        , OLUX_GET_BIT( value, 15 )
        , OLUX_GET_BIT( value, 14 )
        , OLUX_GET_BIT( value, 13 )
        , OLUX_GET_BIT( value, 12 )
        , OLUX_GET_BIT( value, 11 )
        , OLUX_GET_BIT( value, 10 )

        , OLUX_GET_BIT( value, 9 )
        , OLUX_GET_BIT( value, 8 )
        , OLUX_GET_BIT( value, 7 )
        , OLUX_GET_BIT( value, 6 )
        , OLUX_GET_BIT( value, 5 )
        , OLUX_GET_BIT( value, 4 )
        , OLUX_GET_BIT( value, 3 )
        , OLUX_GET_BIT( value, 2 )
        , OLUX_GET_BIT( value, 1 )
        , OLUX_GET_BIT( value, 0 ) );

    printf( "===============================================================================================\n\n" );
}



void ClrScr( void ) {

    write( OLUX_STD_OUT, OLUX_CLEAR_SCREEN, strlen( OLUX_CLEAR_SCREEN ) );
}



s8 NonBlockReadKey( void ) {

    s8 c;

    fcntl( OLUX_STD_IN, F_SETFL, O_NONBLOCK );

    c = GetKey();

    fcntl( OLUX_STD_IN, F_SETFL, O_ASYNC );

    return c;
}



bool ReadLine( s8 *Buf, u32 Length ) {

	u32 i;
	s8 c;


	// Clear buffer
	memset( Buf, 0, Length );
	for( i = 0 ; i < (Length - 1) ; ) {


		// Read a character
		c = getchar();
		if( c == '\n' ) {

			return TRUE;
		}

		
		// Push into buffer
		Buf[ i ] = c;
		i++;
	}


	return FALSE;
}



s8 GetKey( void ) {

    s8 c;
    struct termios orig, new;


    if( tcgetattr( OLUX_STD_IN, &orig ) ) {

        return 0;
    }


    memcpy( &new, &orig, sizeof( struct termios ) );


    new.c_lflag &= ~(ICANON);
    new.c_cc[ VMIN ] = 1;
    new.c_cc[ VTIME ] = 0;


    if( tcsetattr( OLUX_STD_IN, TCSAFLUSH, &new ) ) {

        return 0;
    }


    read( OLUX_STD_IN, &c, 1 );


    if( tcsetattr( OLUX_STD_IN, TCSAFLUSH, &orig ) ) {

        return 0;
    }


    return c;
}



