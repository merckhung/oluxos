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
#include <packet.h>

#include <ncurses.h>
#include <panel.h>

#include <kdbger.h>


static void help( void ) {

    fprintf( stderr, "\nCopyright (c) 2011 Olux Organization All rights reserved.\n\n" );
    fprintf( stderr, "OluxOS Kernel Debugger, Version "KDBGER_VERSION"\n" );
    fprintf( stderr, "Author: Merck Hung <merckhung@gmail.com>\n" );
    fprintf( stderr, "help: kdbger [-d /dev/ttyS0] [-h]\n\n" );
	fprintf( stderr, "\t-d\tUART TTY device, default is " KDBGER_DEF_TTYDEV "\n");
    fprintf( stderr, "\t-h\tPrint help and exit\n\n");
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


s32 verifyResponsePacket( kdbgerCommPkt_t *pKdbgerCommPkt, kdbgerOpCode_t op ) {

	switch( pKdbgerCommPkt->kdbgerCommHdr.opCode ) {

		case KDBGER_RSP_CONNECT:
			if( op != KDBGER_REQ_CONNECT )
				return 1;
			break;

		case KDBGER_RSP_MEM_READ:
			if( op != KDBGER_REQ_MEM_READ )
				return 1;
			break;

		case KDBGER_RSP_MEM_WRITE:
            if( op != KDBGER_REQ_MEM_WRITE )
                return 1;
			break;

		case KDBGER_RSP_IO_READ:
            if( op != KDBGER_REQ_IO_READ )
                return 1;
			break;

		case KDBGER_RSP_IO_WRITE:
            if( op != KDBGER_REQ_IO_WRITE )
                return 1;
			break;

		case KDBGER_RSP_PCI_READ:
            if( op != KDBGER_REQ_PCI_READ )
                return 1;
			break;

		case KDBGER_RSP_PCI_WRITE:
            if( op != KDBGER_REQ_PCI_WRITE )
                return 1;
			break;

		case KDBGER_RSP_IDE_READ:
            if( op != KDBGER_REQ_IDE_READ )
                return 1;
			break;

		case KDBGER_RSP_IDE_WRITE:
            if( op != KDBGER_REQ_IDE_WRITE )
                return 1;
			break;

		case KDBGER_RSP_PCI_LIST:
			if( op != KDBGER_REQ_PCI_LIST )
				return 1;
			break;

		case KDBGER_RSP_E810_LIST:
			if( op != KDBGER_REQ_E810_LIST )
				return 1;
			break;

		case KDBGER_RSP_NACK:
		default:
			return 1;
	}

	return 0;
}


s32 executeFunction( s32 fd, kdbgerOpCode_t op, u64 addr, u32 size, u8 *cntBuf, u8 *pktBuf, s32 lenPktBuf ) {

	kdbgerCommPkt_t *pKdbgerCommPkt = (kdbgerCommPkt_t *)pktBuf;
	s32 rwByte;

	// Clear buffer
	memset( pktBuf, 0, lenPktBuf );

	// Fill in data fields
	switch( op ) {

		case KDBGER_REQ_CONNECT:

			pKdbgerCommPkt->kdbgerCommHdr.pktLen = sizeof( kdbgerCommHdr_t );
			break;

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

		case KDBGER_REQ_IO_READ:

			pKdbgerCommPkt->kdbgerReqIoReadPkt.address = (u16)(addr & 0xFFFFULL);
			pKdbgerCommPkt->kdbgerReqIoReadPkt.size = size;
			pKdbgerCommPkt->kdbgerCommHdr.pktLen = sizeof( kdbgerReqIoReadPkt_t );
			break;

		case KDBGER_REQ_IO_WRITE:

			if( !size || !cntBuf )
				return 1;

			pKdbgerCommPkt->kdbgerReqIoWritePkt.address = (u16)(addr & 0xFFFFULL);
			pKdbgerCommPkt->kdbgerReqIoWritePkt.size = size;
			memcpy( &pKdbgerCommPkt->kdbgerReqIoWritePkt.ioContent, cntBuf, size );
			pKdbgerCommPkt->kdbgerCommHdr.pktLen = 
				sizeof( kdbgerReqIoWritePkt_t ) - sizeof( s8 * ) + size; 
			break;

		case KDBGER_REQ_PCI_READ:

			pKdbgerCommPkt->kdbgerReqPciReadPkt.address = (u32)(addr & 0xFFFFFFFFULL);
			pKdbgerCommPkt->kdbgerReqPciReadPkt.size = (u16)(size & 0xFFFF);
			pKdbgerCommPkt->kdbgerCommHdr.pktLen = sizeof( kdbgerReqPciReadPkt_t );
			break;

		case KDBGER_REQ_PCI_WRITE:

			if( !size || !cntBuf )
				return 1;

			pKdbgerCommPkt->kdbgerReqPciWritePkt.address = (u32)(addr & 0xFFFFFFFFULL);
			pKdbgerCommPkt->kdbgerReqPciWritePkt.size = (u16)(size & 0xFFFF);
			memcpy( &pKdbgerCommPkt->kdbgerReqPciWritePkt.pciContent, cntBuf, size );
			pKdbgerCommPkt->kdbgerCommHdr.pktLen = 
				sizeof( kdbgerReqPciWritePkt_t ) - sizeof( s8 * ) + size; 
			break;

		case KDBGER_REQ_IDE_READ:

			pKdbgerCommPkt->kdbgerReqIdeReadPkt.address = addr;
			pKdbgerCommPkt->kdbgerReqIdeReadPkt.size = size;
			pKdbgerCommPkt->kdbgerCommHdr.pktLen = sizeof( kdbgerReqIdeReadPkt_t );
			break;

		case KDBGER_REQ_IDE_WRITE:

			if( !size || !cntBuf )
				return 1;

			pKdbgerCommPkt->kdbgerReqIdeWritePkt.address = addr;
			pKdbgerCommPkt->kdbgerReqIdeWritePkt.size = size;
			memcpy( &pKdbgerCommPkt->kdbgerReqIdeWritePkt.ideContent, cntBuf, size );
			pKdbgerCommPkt->kdbgerCommHdr.pktLen = 
				sizeof( kdbgerReqIdeWritePkt_t ) - sizeof( s8 * ) + size; 
			break;

		case KDBGER_REQ_PCI_LIST:

			pKdbgerCommPkt->kdbgerCommHdr.pktLen =
				sizeof( kdbgerReqPciWritePkt_t );
			break;

		case KDBGER_REQ_E810_LIST:

			pKdbgerCommPkt->kdbgerCommHdr.pktLen =
				sizeof( kdbgerReqE820ListPkt_t );
			break;

		default:

			fprintf( stderr, "Unsupport operation, %d\n", op );
			return 1;
	}

	// Fill in OpCode
	pKdbgerCommPkt->kdbgerCommHdr.opCode = op;

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


	// Whether it valid or not
	return verifyResponsePacket( pKdbgerCommPkt, op );
}


void initColorPairs( void ) {

    init_pair( WHITE_RED, COLOR_WHITE, COLOR_RED );
    init_pair( WHITE_BLUE, COLOR_WHITE, COLOR_BLUE );
    init_pair( BLACK_WHITE, COLOR_BLACK, COLOR_WHITE );
    init_pair( CYAN_BLUE, COLOR_CYAN, COLOR_BLUE );
    init_pair( RED_BLUE, COLOR_RED, COLOR_BLUE );
    init_pair( YELLOW_BLUE, COLOR_YELLOW, COLOR_BLUE );
    init_pair( BLACK_GREEN, COLOR_BLACK, COLOR_GREEN );
    init_pair( BLACK_YELLOW, COLOR_BLACK, COLOR_YELLOW );
    init_pair( YELLOW_RED, COLOR_YELLOW, COLOR_RED );
    init_pair( YELLOW_BLACK, COLOR_YELLOW, COLOR_BLACK );
    init_pair( WHITE_YELLOW, COLOR_WHITE, COLOR_YELLOW );
	init_pair( RED_WHITE, COLOR_RED, COLOR_WHITE );
}


void printBasePlane( kdbgerUiProperty_t *pKdbgerUiProperty ) {

    // Background Color
    printWindow( 
		pKdbgerUiProperty->kdbgerBasePanel, 
		background,
		KDBGER_MAX_LINE,
		KDBGER_MAX_COLUMN,
		KDBGER_MIN_LINE,
		KDBGER_MIN_COLUMN,
		WHITE_BLUE,
		"" );

    // Logo
    printWindow(
		pKdbgerUiProperty->kdbgerBasePanel,
		logo,
		KDBGER_STRING_NLINE,
		KDBGER_MAX_COLUMN,
		KDBGER_MIN_LINE,
		KDBGER_MIN_COLUMN,
		WHITE_RED,
		KDBGER_PROGRAM_FNAME );

	// Copyright
    printWindow(
		pKdbgerUiProperty->kdbgerBasePanel,
		copyright,
		KDBGER_STRING_NLINE,
		strlen( KDBGER_AUTHER_NAME ),
		KDBGER_MIN_LINE,
		KDBGER_MAX_COLUMN - strlen( KDBGER_AUTHER_NAME ),
		WHITE_RED, KDBGER_AUTHER_NAME );

	// Update screen
    update_panels();
    doupdate();
}


void updateStatusTimer( kdbgerUiProperty_t *pKdbgerUiProperty ) {

	time_t timer;
	struct tm *nowtime;
	u32 thisSecond;
	u8 update = 0;
	s8 stsBuf[ KDBGER_MAX_COLUMN - KDBGER_MAX_TIMESTR ];

	// Read second
	thisSecond = time( &timer );
	if( thisSecond == -1 )
		return;

	// Convert to localtime
	nowtime = localtime( &timer );
	if( !nowtime )
		return;

	if( pKdbgerUiProperty->lastSecond < thisSecond ) {

		// Update timer per second
		printWindowAt( pKdbgerUiProperty->kdbgerBasePanel, 
			time,
			KDBGER_STRING_NLINE,
			KDBGER_MAX_TIMESTR,
			KDBGER_MAX_LINE,
			KDBGER_MAX_COLUMN - KDBGER_MAX_TIMESTR,
			BLACK_WHITE,
			"%2.2d:%2.2d:%2.2d",
			nowtime->tm_hour,
			nowtime->tm_min,
			nowtime->tm_sec );

		update = 1;
	}

	if( pKdbgerUiProperty->statusStr && ((thisSecond - pKdbgerUiProperty->lastSecond) >= KDBGER_STS_INTV_SECS) ) {

		s32 len = strlen( pKdbgerUiProperty->statusStr );
		if( pKdbgerUiProperty->strIdx >= len )
			pKdbgerUiProperty->strIdx = 0;

		if( (len - pKdbgerUiProperty->strIdx) > (KDBGER_MAX_COLUMN - KDBGER_MAX_TIMESTR) )
			len = KDBGER_MAX_COLUMN - KDBGER_MAX_TIMESTR;

		snprintf( stsBuf, len, "%s", (pKdbgerUiProperty->statusStr + pKdbgerUiProperty->strIdx) );

		// Update status bar
    	printWindow( 
			pKdbgerUiProperty->kdbgerBasePanel,
			status,
			KDBGER_STRING_NLINE,
			KDBGER_MAX_COLUMN - KDBGER_MAX_TIMESTR,
			KDBGER_MAX_LINE,
			KDBGER_MIN_COLUMN,
			RED_WHITE,
			"%s",
			stsBuf );

		pKdbgerUiProperty->strIdx++;
		update = 1;
	}

	if( update )
		pKdbgerUiProperty->lastSecond = thisSecond;
}


void printDumpBasePanel( kdbgerUiProperty_t *pKdbgerUiProperty ) {

	// Print Top bar
	printWindowAt( pKdbgerUiProperty->kdbgerDumpPanel,
		top, 
		KDBGER_STRING_NLINE,
		strlen( KDBGER_DUMP_TOP_BAR ),
		KDBGER_DUMP_TOP_LINE,
		KDBGER_DUMP_TOP_COLUMN,
		RED_BLUE,
		KDBGER_DUMP_TOP_BAR );

	// Print Rtop bar
	printWindowAt( pKdbgerUiProperty->kdbgerDumpPanel,
		rtop, 
		KDBGER_STRING_NLINE,
		KDBGER_DUMP_BYTE_PER_LINE,
		KDBGER_DUMP_RTOP_LINE,
		KDBGER_DUMP_RTOP_COLUMN,
		RED_BLUE,
		KDBGER_DUMP_RTOP_BAR );

	// Print Left bar
	printWindowAt( pKdbgerUiProperty->kdbgerDumpPanel,
		left,
		KDBGER_DUMP_BYTE_PER_LINE,
		4,
		KDBGER_DUMP_LEFT_LINE,
		KDBGER_DUMP_LEFT_COLUMN,
		RED_BLUE,
		KDBGER_DUMP_LEFT_BAR );

	// Print Info base
	printWindowAt( pKdbgerUiProperty->kdbgerDumpPanel,
		info,
		KDBGER_STRING_NLINE,
		KDBGER_MAX_COLUMN,
		KDBGER_INFO_LINE,
		KDBGER_INFO_COLUMN,
		WHITE_BLUE,
		"%s",
		pKdbgerUiProperty->infoStr );
}


void printDumpUpdatePanel( kdbgerUiProperty_t *pKdbgerUiProperty ) {

	s32 i, x, y;
	u8 valueBuf[ KDBGER_DUMP_VBUF_SZ + 1 ];
	u8 asciiBuf[ KDBGER_DUMP_ABUF_SZ + 1 ];
	u8 *vp = valueBuf, *ap = asciiBuf;
	u8 *dataPtr = (u8 *)&pKdbgerUiProperty->pKdbgerCommPkt->kdbgerRspMemReadPkt.memContent;
	u8 *pDataPtr = dataPtr;

	// Terminate buffers
	valueBuf[ KDBGER_DUMP_VBUF_SZ ] = 0;
	asciiBuf[ KDBGER_DUMP_ABUF_SZ ] = 0;

	// Format data for value & ascii
	for( i = 0 ; i < KDBGER_DUMP_BYTE_PER_LINE ; i++ ) {
		 
		vp += sprintf( (s8 *)vp,
		"%2.2X %2.2X %2.2X %2.2X %2.2X %2.2X %2.2X %2.2X %2.2X %2.2X %2.2X %2.2X %2.2X %2.2X %2.2X %2.2X",
		dataPtr[ 0 ], dataPtr[ 1 ], dataPtr[ 2 ], dataPtr[ 3 ], dataPtr[ 4 ], dataPtr[ 5 ], dataPtr[ 6 ],
		dataPtr[ 7 ], dataPtr[ 8 ], dataPtr[ 9 ], dataPtr[ 10 ], dataPtr[ 11 ], dataPtr[ 12 ], dataPtr[ 13 ],
		dataPtr[ 14 ], dataPtr[ 15 ] );

		ap += sprintf( (s8 *)ap,
		"%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c",
		KDBGER_DUMP_ASCII_FILTER( dataPtr[ 0 ] ),
		KDBGER_DUMP_ASCII_FILTER( dataPtr[ 1 ] ),
        KDBGER_DUMP_ASCII_FILTER( dataPtr[ 2 ] ),
        KDBGER_DUMP_ASCII_FILTER( dataPtr[ 3 ] ),
        KDBGER_DUMP_ASCII_FILTER( dataPtr[ 4 ] ),
        KDBGER_DUMP_ASCII_FILTER( dataPtr[ 5 ] ),
        KDBGER_DUMP_ASCII_FILTER( dataPtr[ 6 ] ),
        KDBGER_DUMP_ASCII_FILTER( dataPtr[ 7 ] ),
        KDBGER_DUMP_ASCII_FILTER( dataPtr[ 8 ] ),
        KDBGER_DUMP_ASCII_FILTER( dataPtr[ 9 ] ),
        KDBGER_DUMP_ASCII_FILTER( dataPtr[ 10 ] ),
        KDBGER_DUMP_ASCII_FILTER( dataPtr[ 11 ] ),
        KDBGER_DUMP_ASCII_FILTER( dataPtr[ 12 ] ),
        KDBGER_DUMP_ASCII_FILTER( dataPtr[ 13 ] ),
        KDBGER_DUMP_ASCII_FILTER( dataPtr[ 14 ] ),
        KDBGER_DUMP_ASCII_FILTER( dataPtr[ 15 ] ) );

		// Move to next line
		dataPtr += KDBGER_DUMP_BYTE_PER_LINE;
	}

	// Print value
	printWindowAt(
		pKdbgerUiProperty->kdbgerDumpPanel,
		value, 
		KDBGER_DUMP_BYTE_PER_LINE,
		KDBGER_DUMP_BUF_PER_LINE,
		KDBGER_DUMP_VALUE_LINE,
		KDBGER_DUMP_VALUE_COLUMN,
		WHITE_BLUE,
		"%s",
		valueBuf );

	// Print ASCII
	printWindowAt(
		pKdbgerUiProperty->kdbgerDumpPanel,
		ascii,
		KDBGER_DUMP_BYTE_PER_LINE,
		KDBGER_DUMP_BYTE_PER_LINE,
		KDBGER_DUMP_ASCII_LINE,
		KDBGER_DUMP_ASCII_COLUMN,
		WHITE_BLUE,
		"%s",
		asciiBuf );

	// Print Offset bar
	printWindowAt(
		pKdbgerUiProperty->kdbgerDumpPanel,
		offset, 
		KDBGER_STRING_NLINE,
		4,
		KDBGER_DUMP_OFF_LINE,
		KDBGER_DUMP_OFF_COLUMN,
		RED_BLUE,
		"%4.4X",
		pKdbgerUiProperty->dumpByteOffset );

	// Print base address
	printWindowAt(
		pKdbgerUiProperty->kdbgerDumpPanel,
		baseaddr, 
		KDBGER_STRING_NLINE,
		20,
		KDBGER_DUMP_BASEADDR_LINE,
		strlen( KDBGER_INFO_MEMORY_BASE ),
		WHITE_BLUE,
		KDBGER_INFO_MEMORY_BASE_FMT,
		(u32)(pKdbgerUiProperty->dumpByteBase >> 32),
		(u32)(pKdbgerUiProperty->dumpByteBase & 0xFFFFFFFFULL) );

	// Highlight
	y = (pKdbgerUiProperty->dumpByteOffset / KDBGER_DUMP_BYTE_PER_LINE) + KDBGER_DUMP_VALUE_LINE;
	x = ((pKdbgerUiProperty->dumpByteOffset % KDBGER_DUMP_BYTE_PER_LINE) * 3) + KDBGER_DUMP_VALUE_COLUMN;
	printWindowMove(
		pKdbgerUiProperty->kdbgerDumpPanel,
		highlight,
		1,
		2,
		y,
		x,
		YELLOW_BLUE,
		"%2.2X",
		*(pDataPtr + pKdbgerUiProperty->dumpByteOffset) );
}


void handleKeyPressForDumpPanel( kdbgerUiProperty_t *pKdbgerUiProperty ) {

	switch( pKdbgerUiProperty->inputBuf ) {

		case KBPRS_UP:

			if( (pKdbgerUiProperty->dumpByteOffset - KDBGER_DUMP_BYTE_PER_LINE) >= 0 )
				pKdbgerUiProperty->dumpByteOffset -= KDBGER_DUMP_BYTE_PER_LINE;
			else
				pKdbgerUiProperty->dumpByteOffset = KDBGER_BYTE_PER_SCREEN - (KDBGER_DUMP_BYTE_PER_LINE - pKdbgerUiProperty->dumpByteOffset);
            break;

		case KBPRS_DOWN:

			if( (pKdbgerUiProperty->dumpByteOffset + KDBGER_DUMP_BYTE_PER_LINE) < KDBGER_BYTE_PER_SCREEN )
				pKdbgerUiProperty->dumpByteOffset += KDBGER_DUMP_BYTE_PER_LINE;
			else
				pKdbgerUiProperty->dumpByteOffset %= KDBGER_DUMP_BYTE_PER_LINE;
            break;

		case KBPRS_LEFT:

			if( (pKdbgerUiProperty->dumpByteOffset - 1) >= 0 && (pKdbgerUiProperty->dumpByteOffset % KDBGER_DUMP_BYTE_PER_LINE) > 0 )
				pKdbgerUiProperty->dumpByteOffset--;
			else
				pKdbgerUiProperty->dumpByteOffset += (KDBGER_DUMP_BYTE_PER_LINE - 1);
            break;

		case KBPRS_RIGHT:

			if( (pKdbgerUiProperty->dumpByteOffset + 1) < KDBGER_BYTE_PER_SCREEN 
				&& ((pKdbgerUiProperty->dumpByteOffset + 1) % KDBGER_DUMP_BYTE_PER_LINE) )
				pKdbgerUiProperty->dumpByteOffset++;
			else
				pKdbgerUiProperty->dumpByteOffset -= (KDBGER_DUMP_BYTE_PER_LINE - 1);
            break;

		case KBPRS_PGUP:

			pKdbgerUiProperty->dumpByteBase -= KDBGER_BYTE_PER_SCREEN;
            break;

		case KBPRS_PGDN:

			pKdbgerUiProperty->dumpByteBase += KDBGER_BYTE_PER_SCREEN;
            break;

		case KBPRS_SPACE:

			break;

		default:
			break;
	}
}


s32 connectToOluxOSKernel( kdbgerUiProperty_t *pKdbgerUiProperty ) {

	// Connect to OluxOS Kernel
	return executeFunction( 
			pKdbgerUiProperty->fd,
			KDBGER_REQ_CONNECT,
			0,
			0,
			NULL,
			pKdbgerUiProperty->pktBuf,
			KDBGER_MAXSZ_PKT );
}


s32 readPciList( kdbgerUiProperty_t *pKdbgerUiProperty ) {

	kdbgerRspPciListPkt_t *pKdbgerRspPciListPkt;

	// Read PCI list
	if( executeFunction( pKdbgerUiProperty->fd, KDBGER_REQ_PCI_LIST, 0, 0, NULL, pKdbgerUiProperty->pktBuf, KDBGER_MAXSZ_PKT ) )
		return -1;

	// Save PCI list
	pKdbgerRspPciListPkt = (kdbgerRspPciListPkt_t *)pKdbgerUiProperty->pktBuf;
	pKdbgerUiProperty->numOfPciDevice = pKdbgerRspPciListPkt->numOfPciDevice;
	pKdbgerUiProperty->pKdbgerPciDev = (kdbgerPciDev_t *)malloc( sizeof( kdbgerPciDev_t ) * pKdbgerUiProperty->numOfPciDevice );

	if( !pKdbgerUiProperty->pKdbgerPciDev )
		return -1;

	memcpy( pKdbgerUiProperty->pKdbgerPciDev, &pKdbgerRspPciListPkt->pciListContent, sizeof( kdbgerPciDev_t ) * pKdbgerUiProperty->numOfPciDevice );

	return 0;
}


s32 readE820List( kdbgerUiProperty_t *pKdbgerUiProperty ) {

	kdbgerRspE820ListPkt_t *pKdbgerRspE820ListPkt;

	// Read E820 list
	if( executeFunction( pKdbgerUiProperty->fd, KDBGER_REQ_E810_LIST, 0, 0, NULL, pKdbgerUiProperty->pktBuf, KDBGER_MAXSZ_PKT ) )
		return -1;

	// Save E820 list
	pKdbgerRspE820ListPkt = (kdbgerRspE820ListPkt_t *)pKdbgerUiProperty->pktBuf;
	pKdbgerUiProperty->numOfE820Record = pKdbgerRspE820ListPkt->numOfE820Record;
	pKdbgerUiProperty->pKdbgerE820record = (kdbgerE820record_t *)malloc( sizeof( kdbgerE820record_t ) * pKdbgerUiProperty->numOfE820Record );

	if( !pKdbgerUiProperty->pKdbgerE820record )
		return -1;

	memcpy( pKdbgerUiProperty->pKdbgerE820record, &pKdbgerRspE820ListPkt->e820ListContent, sizeof( kdbgerE820record_t ) * pKdbgerUiProperty->numOfE820Record );

	return 0;
}


s32 readMemory( kdbgerUiProperty_t *pKdbgerUiProperty ) {

	// Read memory
	return executeFunction(
			pKdbgerUiProperty->fd,
			KDBGER_REQ_MEM_READ,
			pKdbgerUiProperty->dumpByteBase,
			KDBGER_BYTE_PER_SCREEN,
			NULL,
			pKdbgerUiProperty->pktBuf,
			KDBGER_MAXSZ_PKT );
}


s32 main( s32 argc, s8 **argv ) {

	s8 c, ttyDevice[ KDBGER_MAX_PATH ];
	kdbgerUiProperty_t kdbgerUiProperty =
	{ 0, 0, KHF_MEM, KHF_MEM, { 0 }, NULL,
		{ NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL },
		{ NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
			NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL },
	 0, NULL, NULL, 0, 0, 0, NULL, 0, NULL, 0 };


	// Initialization
	strncpy( ttyDevice, KDBGER_DEF_TTYDEV, KDBGER_MAX_PATH );
	kdbgerUiProperty.pKdbgerCommPkt = (kdbgerCommPkt_t *)kdbgerUiProperty.pktBuf;
	kdbgerUiProperty.statusStr = KDBGER_HELP_TXT;


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
	kdbgerUiProperty.fd = open( ttyDevice, O_RDWR );
	if( kdbgerUiProperty.fd < 0 ) {

		fprintf( stderr, "Cannot open device %s\n", ttyDevice );
		return 1;
	}


	// Configure TTY device
	if( configureTtyDevice( kdbgerUiProperty.fd ) ) {

		fprintf( stderr, "Cannot configure device %s\n", ttyDevice );
		goto ErrExit;
	}


	// Connect to OluxOS Kernel
	if( connectToOluxOSKernel( &kdbgerUiProperty ) ) {

		fprintf( stderr, "Cannot connect to OluxOS Kernel via %s\n", ttyDevice );
		goto ErrExit;
	}

	// Read PCI list
	if( readPciList( &kdbgerUiProperty ) ) {

		fprintf( stderr, "Cannot get PCI device listing\n" );
		goto ErrExit;
	}

	// Read E820 list
	if( readE820List( &kdbgerUiProperty ) ) {

		fprintf( stderr, "Cannot get E820 listing\n" );
		goto ErrExit1;
	}


	// Initialize ncurses
    initscr();
    start_color();
    cbreak();
    noecho();
    nodelay( stdscr, 1 );
    keypad( stdscr, 1 );
    curs_set( 0 );
	initColorPairs();


	// Print base panel
	printBasePlane( &kdbgerUiProperty );

	kdbgerUiProperty.infoStr = KDBGER_INFO_MEMORY_BASE;
	printDumpBasePanel( &kdbgerUiProperty );


	// Main loop
	for( ; ; ) {

		// Get keyboard input
		kdbgerUiProperty.inputBuf = getch();
		switch( kdbgerUiProperty.inputBuf ) {

			// ESC
			case KBPRS_ESC:
				goto Exit;

			// Help
			case KBPRS_F1:
				break;

			// PCI/PCI-E listing
			case KBPRS_U_L:
			case KBPRS_L_L:
				kdbgerUiProperty.kdbgerHwFunc = KHF_PCIL;
				break;

			// PCI/PCI-E config space
			case KBPRS_U_P:
			case KBPRS_L_P:
				kdbgerUiProperty.kdbgerHwFunc = KHF_PCI;
				break;

			// I/O
			case KBPRS_U_I:
			case KBPRS_L_I:
				kdbgerUiProperty.kdbgerHwFunc = KHF_IO;
				break;

			// Memory
			default:
			case KBPRS_U_M:
			case KBPRS_L_M:
				kdbgerUiProperty.kdbgerHwFunc = KHF_MEM;
				break;
		}


		switch( kdbgerUiProperty.kdbgerHwFunc ) {

			default:
			case KHF_MEM:

				handleKeyPressForDumpPanel( &kdbgerUiProperty );
				if( !readMemory( &kdbgerUiProperty ) )
					printDumpUpdatePanel( &kdbgerUiProperty );

				break;

			case KHF_IO:
				break;

			case KHF_PCI:
				break;

			case KHF_PCIL:
				break;
		}


        // Update timer
		updateStatusTimer( &kdbgerUiProperty );

		// Refresh Screen
		update_panels();
		doupdate();

		// Delay for a while
		usleep( 30000 );
	}

Exit:

	// Terminate ncurses
	endwin();


/*
	// Write memory
	memset( cntBuf, 0x99, 256 );
    if( executeFunction( fd, KDBGER_REQ_MEM_WRITE, 0x7c00, 256, cntBuf, pktBuf, KDBGER_MAXSZ_PKT ) ) {

        fprintf( stderr, "Error on issuing memory writing request\n" );
        goto ErrExit;
    }


	// Read IO
	if( executeFunction( fd, KDBGER_REQ_IO_READ, 0x00, 256, NULL, pktBuf, KDBGER_MAXSZ_PKT ) ) {

		fprintf( stderr, "Error on issuing memory reading request\n" );
		goto ErrExit;
	}
	debugPrintBuffer( (s8 *)&pKdbgerCommPkt->kdbgerRspIoReadPkt.ioContent,
		pKdbgerCommPkt->kdbgerRspIoReadPkt.size, (u32)pKdbgerCommPkt->kdbgerRspIoReadPkt.address );
*/


	free( kdbgerUiProperty.pKdbgerE820record );

ErrExit1:
	free( kdbgerUiProperty.pKdbgerPciDev );

ErrExit:
	// Close TTY device
	close( kdbgerUiProperty.fd );

    return 0;
}



