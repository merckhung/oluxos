/*
 * Copyright (C) 2011 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * File: kdbger.c
 * Description:
 *	OluxOS Kernel Debugger
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include <termios.h>

#include <otypes.h>
#include <kdbger.h>
#include <packet.h>


static void help( void ) {

    fprintf( stderr, "\nCopyright (c) 2011 Olux Organization All rights reserved.\n\n" );
    fprintf( stderr, "OluxOS Kernel Debugger, Version "KDBGER_VERSION"\n\n" );

    fprintf( stderr, "Authors: Merck Hung <merck@olux.org>\n\n" );

    fprintf( stderr, "help: kdbger \n\n" );
    fprintf( stderr, "\t-h\tPrint help and exit\n");
	fprintf( stderr, "\t-v\tVerbose more message\n");
}


static void debugPrintBuffer( s8 *pBuf, u32 size, u32 base ) {
#define LINE_DIGITS	16

    u32 i, j;
	u8 buf[ LINE_DIGITS ];
	s8 unalign = 0;

	if( size % LINE_DIGITS ) {

		unalign = 1;
	}

    printf( " Address | 00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F|   ASCII DATA   \n" );
    printf( "---------------------------------------------------------------------------\n" );

    for( i = 0 ; i <= size ; i++ ) {

        if( !(i % LINE_DIGITS) ) {

			if( i ) {
				for( j = 0 ; j < LINE_DIGITS ; j++ ) {

					if( buf[ j ] >= '!' && buf[ j ] <= '~' )
						printf( "%c", buf[ j ] );
					else
						printf( "." );
				}
				printf( "\n" );
			}

			if( i == size )
				break;

            printf( "%8.8X : ", i + base );
			memset( buf, ' ', sizeof( buf ) );
        }

		buf[ i % LINE_DIGITS ] = (u8)(*(pBuf + i));
        printf( "%2.2X ", buf[ i % LINE_DIGITS ] & 0xFF );
    }

	if( unalign ) {

		for( j = LINE_DIGITS - (size % LINE_DIGITS) - 1 ; j-- ; ) 
			printf( "   " );

		for( j = 0 ; j < (size % LINE_DIGITS) ; j++ )
			if( buf[ j ] >= '!' && buf[ j ] <= '~' )
				printf( "%c", buf[ j ] );
			else
				printf( "." );

		printf( "\n" );
	}

    printf( "---------------------------------------------------------------------------\n" );
}


static s32 configureTtyDevice( s32 fd ) {

	struct termios termSetting;

	if( tcgetattr( fd, &termSetting ) )
		return 1;

	if( cfsetspeed( &termSetting, B115200 ) )
		return 1;

    if( tcsetattr( fd, TCSANOW, &termSetting ) )
        return 1;

	return 0;
}


s32 manipulateMemory( s32 fd, kdbgerOpCode_t op, u64 addr, u32 size, s8 *cntBuf, s8 *pktBuf, s32 lenPktBuf ) {

	kdbgerCommPkt_t *pKdbgerCommPkt = (kdbgerCommPkt_t *)pktBuf;
	s32 rwByte;

	// Clear buffer
	memset( pktBuf, 0, lenPktBuf );

	// Fill in data fields
	switch( op ) {

		case KDBGER_REQ_MEM_READ:

			pKdbgerCommPkt->kdbgerReqMemReadPkt.address = addr;
			pKdbgerCommPkt->kdbgerReqMemReadPkt.size = size;
			pKdbgerCommPkt->kdbgerCommHdr.pktLen = sizeof( kdbgerReqMemReadPkt_t );
			break;

		case KDBGER_REQ_MEM_WRITE:

			if( !size || !cntBuf )
				return 1;

			pKdbgerCommPkt->kdbgerReqMemWritePkt.address = addr;
			pKdbgerCommPkt->kdbgerReqMemWritePkt.size = size;
			memcpy( &pKdbgerCommPkt->kdbgerReqMemWritePkt.memContent, cntBuf, size );
			pKdbgerCommPkt->kdbgerCommHdr.pktLen = 
				sizeof( kdbgerReqMemWritePkt_t ) - sizeof( s8 * ) + size; 
			break;  

		default:

			fprintf( stderr, "Unsupport operation, %d\n", op );
			return 1;
	}

	// Fill in OpCode
	pKdbgerCommPkt->kdbgerCommHdr.opCode = op;

	printf( "pktLen = %d\n", pKdbgerCommPkt->kdbgerCommHdr.pktLen );

	// Transmit the request
	rwByte = write( fd, pktBuf, pKdbgerCommPkt->kdbgerCommHdr.pktLen );
	if( rwByte != pKdbgerCommPkt->kdbgerCommHdr.pktLen ) {

		fprintf( stderr, "Error on transmitting request wsize = %d\n", rwByte );
		return 1;
	}

	// Receive the response
	memset( pktBuf, 0, lenPktBuf );
	rwByte = read( fd, pktBuf, lenPktBuf );
	if( !rwByte ) {

		fprintf( stderr, "No response\n" );
		return 1;
	}

	printf( "rwByte = %d\n", rwByte );
	printf( "opCode = 0x%4.4X\n", pKdbgerCommPkt->kdbgerCommHdr.opCode );
	printf( "pktLen = 0x%8.8X\n", pKdbgerCommPkt->kdbgerCommHdr.pktLen );
	printf( "errCod = 0x%4.4X\n", pKdbgerCommPkt->kdbgerCommHdr.errorCode );

	return 0;
}


s32 main( s32 argc, s8 **argv ) {

	s32 fd;
	s8 ttyDevice[ KDBGER_MAX_PATH ];
	s8 c;
	s8 pktBuf[ KDBGER_MAXSZ_PKT ];
	s8 cntBuf[ KDBGER_MAXSZ_PKT ];
	kdbgerCommPkt_t *pKdbgerCommPkt = (kdbgerCommPkt_t *)pktBuf;


	// Initialization
	strncpy( ttyDevice, KDBGER_DEF_TTYDEV, KDBGER_MAX_PATH );


	// Handle parameters
    while( (c = getopt( argc, argv, "d:h" )) != EOF ) {

        switch( c ) {

			case 'd':

				strncpy( ttyDevice, optarg, KDBGER_MAX_PATH );
				break;

            case 'h':

                help();
                return 0;

            default:

                help();
                return 1;
        }
    }


	// Open TTY device
	fd = open( ttyDevice, O_RDWR );
	if( fd < 0 ) {

		fprintf( stderr, "Cannot open device %s\n", ttyDevice );
		return 1;
	}


	// Configure TTY device
	if( configureTtyDevice( fd ) ) {

		fprintf( stderr, "Cannot configure device %s\n", ttyDevice );
		goto ErrExit;
	}


	// Read memory
	if( manipulateMemory( fd, KDBGER_REQ_MEM_READ, 0x7c00, 256, NULL, pktBuf, KDBGER_MAXSZ_PKT ) ) {

		fprintf( stderr, "Error on issuing memory reading request\n" );
		goto ErrExit;
	}
	debugPrintBuffer( (s8 *)&pKdbgerCommPkt->kdbgerRspMemReadPkt.memContent,
		250, (u32)pKdbgerCommPkt->kdbgerRspMemReadPkt.address );


	// Write memory
	memset( cntBuf, 0x38, 256 );
    if( manipulateMemory( fd, KDBGER_REQ_MEM_WRITE, 0x7c00, 256, cntBuf, pktBuf, KDBGER_MAXSZ_PKT ) ) {

        fprintf( stderr, "Error on issuing memory writing request\n" );
        goto ErrExit;
    }


	// Read memory
	if( manipulateMemory( fd, KDBGER_REQ_MEM_READ, 0x7c00, 256, NULL, pktBuf, KDBGER_MAXSZ_PKT ) ) {

		fprintf( stderr, "Error on issuing memory reading request\n" );
		goto ErrExit;
	}
	debugPrintBuffer( (s8 *)&pKdbgerCommPkt->kdbgerRspMemReadPkt.memContent,
		pKdbgerCommPkt->kdbgerRspMemReadPkt.size, (u32)pKdbgerCommPkt->kdbgerRspMemReadPkt.address );


ErrExit:

	// Close TTY device
	close( fd );

    return 0;
}



