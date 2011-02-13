/*
 * Copyright (C) 2011 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * File: dumpPanel.c
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


void clearDumpBasePanel( kdbgerUiProperty_t *pKdbgerUiProperty ) {

	destroyWindow( pKdbgerUiProperty->kdbgerDumpPanel, top );
	destroyWindow( pKdbgerUiProperty->kdbgerDumpPanel, rtop );
	destroyWindow( pKdbgerUiProperty->kdbgerDumpPanel, left );
	destroyWindow( pKdbgerUiProperty->kdbgerDumpPanel, info );
}


void clearDumpUpdatePanel( kdbgerUiProperty_t *pKdbgerUiProperty ) {

	destroyWindow( pKdbgerUiProperty->kdbgerDumpPanel, value );
	destroyWindow( pKdbgerUiProperty->kdbgerDumpPanel, ascii );
	destroyWindow( pKdbgerUiProperty->kdbgerDumpPanel, offset );
	destroyWindow( pKdbgerUiProperty->kdbgerDumpPanel, baseaddr );
	destroyWindow( pKdbgerUiProperty->kdbgerDumpPanel, highlight );
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
	switch( pKdbgerUiProperty->kdbgerHwFunc ) {

		case KHF_MEM:
			printWindowAt(
				pKdbgerUiProperty->kdbgerDumpPanel,
				baseaddr, 
				KDBGER_STRING_NLINE,
				20,
				KDBGER_DUMP_BASEADDR_LINE,
				strlen( pKdbgerUiProperty->infoStr ),
				WHITE_BLUE,
				KDBGER_INFO_MEMORY_BASE_FMT,
				(u32)(pKdbgerUiProperty->dumpByteBase >> 32),
				(u32)(pKdbgerUiProperty->dumpByteBase & 0xFFFFFFFFULL) );
			break;

		case KHF_IO:
			printWindowAt(
				pKdbgerUiProperty->kdbgerDumpPanel,
				baseaddr, 
				KDBGER_STRING_NLINE,
				20,
				KDBGER_DUMP_BASEADDR_LINE,
				strlen( pKdbgerUiProperty->infoStr ),
				WHITE_BLUE,
				KDBGER_INFO_IO_BASE_FMT,
				(u32)(pKdbgerUiProperty->dumpByteBase & 0x0000FFFFULL) );
			break;

		case KHF_PCI:
			break;

		case KHF_PCIL:
			break;

		case KHF_IDE:
			break;

		default:
			break;
	}

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


