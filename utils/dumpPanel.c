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
#include <olux.h>
#include <packet.h>

#include <ncurses.h>
#include <panel.h>

#include <kdbger.h>


static u32 editorColorCount = 0;


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
		pKdbgerUiProperty->kdbgerDumpPanel.infoStr );
}


void printDumpUpdatePanel( kdbgerUiProperty_t *pKdbgerUiProperty ) {

	s32 i, x, y, color;
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
		pKdbgerUiProperty->kdbgerDumpPanel.dumpByteOffset );

	// Print base address
	switch( pKdbgerUiProperty->kdbgerHwFunc ) {

		case KHF_MEM:
			printWindowAt(
				pKdbgerUiProperty->kdbgerDumpPanel,
				baseaddr, 
				KDBGER_STRING_NLINE,
				20,
				KDBGER_DUMP_BASEADDR_LINE,
				strlen( pKdbgerUiProperty->kdbgerDumpPanel.infoStr ),
				WHITE_BLUE,
				KDBGER_INFO_MEMORY_BASE_FMT,
				(u32)(pKdbgerUiProperty->kdbgerDumpPanel.dumpByteBase >> 32),
				(u32)(pKdbgerUiProperty->kdbgerDumpPanel.dumpByteBase & 0xFFFFFFFFULL) );
			break;

		case KHF_IO:
			printWindowAt(
				pKdbgerUiProperty->kdbgerDumpPanel,
				baseaddr, 
				KDBGER_STRING_NLINE,
				20,
				KDBGER_DUMP_BASEADDR_LINE,
				strlen( pKdbgerUiProperty->kdbgerDumpPanel.infoStr ),
				WHITE_BLUE,
				KDBGER_INFO_IO_BASE_FMT,
				(u32)(pKdbgerUiProperty->kdbgerDumpPanel.dumpByteBase & 0x0000FFFFULL) );
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


	// Highlight & Editing
	y = (pKdbgerUiProperty->kdbgerDumpPanel.dumpByteOffset / KDBGER_DUMP_BYTE_PER_LINE) + KDBGER_DUMP_VALUE_LINE;
	x = ((pKdbgerUiProperty->kdbgerDumpPanel.dumpByteOffset % KDBGER_DUMP_BYTE_PER_LINE) * 3) + KDBGER_DUMP_VALUE_COLUMN;
	if( pKdbgerUiProperty->kdbgerDumpPanel.toggleEditing ) {

		color = (editorColorCount++ % 2) ? YELLOW_RED : YELLOW_BLACK;

		printWindowMove(
			pKdbgerUiProperty->kdbgerDumpPanel,
			highlight,
			KDBGER_STRING_NLINE,
			KDBGER_DUMP_HL_DIGITS,
			y,
			x,
			color,
			"%2.2X",
			pKdbgerUiProperty->kdbgerDumpPanel.editingBuf );
		}
	else {

		printWindowMove(
			pKdbgerUiProperty->kdbgerDumpPanel,
			highlight,
			KDBGER_STRING_NLINE,
			KDBGER_DUMP_HL_DIGITS,
			y,
			x,
			YELLOW_RED,
			"%2.2X",
			*(pDataPtr + pKdbgerUiProperty->kdbgerDumpPanel.dumpByteOffset) );

		pKdbgerUiProperty->kdbgerDumpPanel.editingBuf = *(pDataPtr + pKdbgerUiProperty->kdbgerDumpPanel.dumpByteOffset);
	}


	// Bits
	if( pKdbgerUiProperty->kdbgerDumpPanel.toggleBits ) {

		printWindowMove(
			pKdbgerUiProperty->kdbgerDumpPanel,
			bits,
			KDBGER_STRING_NLINE,
			KDBGER_DUMP_BITS_DIGITS,
			y + 1,
			x,
			WHITE_RED,
			"%d%d%d%d_%d%d%d%d",
			OLUX_GET_BIT( *(pDataPtr + pKdbgerUiProperty->kdbgerDumpPanel.dumpByteOffset), 7 ),			
			OLUX_GET_BIT( *(pDataPtr + pKdbgerUiProperty->kdbgerDumpPanel.dumpByteOffset), 6 ),
			OLUX_GET_BIT( *(pDataPtr + pKdbgerUiProperty->kdbgerDumpPanel.dumpByteOffset), 5 ),
			OLUX_GET_BIT( *(pDataPtr + pKdbgerUiProperty->kdbgerDumpPanel.dumpByteOffset), 4 ),
			OLUX_GET_BIT( *(pDataPtr + pKdbgerUiProperty->kdbgerDumpPanel.dumpByteOffset), 3 ),
			OLUX_GET_BIT( *(pDataPtr + pKdbgerUiProperty->kdbgerDumpPanel.dumpByteOffset), 2 ),
			OLUX_GET_BIT( *(pDataPtr + pKdbgerUiProperty->kdbgerDumpPanel.dumpByteOffset), 1 ),
			OLUX_GET_BIT( *(pDataPtr + pKdbgerUiProperty->kdbgerDumpPanel.dumpByteOffset), 0 ) );
	}
	else
		destroyWindow( pKdbgerUiProperty->kdbgerDumpPanel, bits );
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
	destroyWindow( pKdbgerUiProperty->kdbgerDumpPanel, bits );
}


void handleKeyPressForDumpPanel( kdbgerUiProperty_t *pKdbgerUiProperty ) {

	if( pKdbgerUiProperty->kdbgerDumpPanel.toggleEditing ) {

		// Trigger write action
		if( pKdbgerUiProperty->inputBuf == KBPRS_ENTER ) {

			switch( pKdbgerUiProperty->kdbgerHwFunc ) {

				case KHF_MEM:
					writeMemoryByEditing( pKdbgerUiProperty );
					break;

				case KHF_IO:
					writeIoByEditing( pKdbgerUiProperty );
					break;

				case KHF_PCI:
					break;

				case KHF_IDE:
					break;

				default:
					break;
			}

			// Exit
			pKdbgerUiProperty->kdbgerDumpPanel.toggleEditing = 0;
			return;
		}

		// Editing
		if( (pKdbgerUiProperty->inputBuf >= '0' 
			&& pKdbgerUiProperty->inputBuf <= '9')
			|| (pKdbgerUiProperty->inputBuf >= 'a'
			&& pKdbgerUiProperty->inputBuf <= 'f')
			|| (pKdbgerUiProperty->inputBuf >= 'A'
			&& pKdbgerUiProperty->inputBuf <= 'F') ) {

			// Left shift 4 bits
			pKdbgerUiProperty->kdbgerDumpPanel.editingBuf <<= 4;

			if( pKdbgerUiProperty->inputBuf <= '9' ) {

				pKdbgerUiProperty->kdbgerDumpPanel.editingBuf |=
					(u8)((pKdbgerUiProperty->inputBuf - 0x30) & 0x0F);
			}
			else if( pKdbgerUiProperty->inputBuf > 'F' ) {

				pKdbgerUiProperty->kdbgerDumpPanel.editingBuf |=
					(u8)((pKdbgerUiProperty->inputBuf - 0x60 + 9) & 0x0F);
			}
			else {

				pKdbgerUiProperty->kdbgerDumpPanel.editingBuf |=
					(u8)((pKdbgerUiProperty->inputBuf - 0x40 + 9) & 0x0F);
			}
		}

		return;
	}

	switch( pKdbgerUiProperty->inputBuf ) {

		case KBPRS_UP:

			if( (pKdbgerUiProperty->kdbgerDumpPanel.dumpByteOffset - KDBGER_DUMP_BYTE_PER_LINE) >= 0 )
				pKdbgerUiProperty->kdbgerDumpPanel.dumpByteOffset -= KDBGER_DUMP_BYTE_PER_LINE;
			else
				pKdbgerUiProperty->kdbgerDumpPanel.dumpByteOffset = KDBGER_BYTE_PER_SCREEN - (KDBGER_DUMP_BYTE_PER_LINE - pKdbgerUiProperty->kdbgerDumpPanel.dumpByteOffset);
            break;

		case KBPRS_DOWN:

			if( (pKdbgerUiProperty->kdbgerDumpPanel.dumpByteOffset + KDBGER_DUMP_BYTE_PER_LINE) < KDBGER_BYTE_PER_SCREEN )
				pKdbgerUiProperty->kdbgerDumpPanel.dumpByteOffset += KDBGER_DUMP_BYTE_PER_LINE;
			else
				pKdbgerUiProperty->kdbgerDumpPanel.dumpByteOffset %= KDBGER_DUMP_BYTE_PER_LINE;
            break;

		case KBPRS_LEFT:

			if( (pKdbgerUiProperty->kdbgerDumpPanel.dumpByteOffset - 1) >= 0 && (pKdbgerUiProperty->kdbgerDumpPanel.dumpByteOffset % KDBGER_DUMP_BYTE_PER_LINE) > 0 )
				pKdbgerUiProperty->kdbgerDumpPanel.dumpByteOffset--;
			else
				pKdbgerUiProperty->kdbgerDumpPanel.dumpByteOffset += (KDBGER_DUMP_BYTE_PER_LINE - 1);
            break;

		case KBPRS_RIGHT:

			if( (pKdbgerUiProperty->kdbgerDumpPanel.dumpByteOffset + 1) < KDBGER_BYTE_PER_SCREEN 
				&& ((pKdbgerUiProperty->kdbgerDumpPanel.dumpByteOffset + 1) % KDBGER_DUMP_BYTE_PER_LINE) )
				pKdbgerUiProperty->kdbgerDumpPanel.dumpByteOffset++;
			else
				pKdbgerUiProperty->kdbgerDumpPanel.dumpByteOffset -= (KDBGER_DUMP_BYTE_PER_LINE - 1);
            break;

		case KBPRS_PGUP:

			pKdbgerUiProperty->kdbgerDumpPanel.dumpByteBase -= KDBGER_BYTE_PER_SCREEN;
            break;

		case KBPRS_PGDN:

			pKdbgerUiProperty->kdbgerDumpPanel.dumpByteBase += KDBGER_BYTE_PER_SCREEN;
            break;

		case KBPRS_ENTER:

			pKdbgerUiProperty->kdbgerDumpPanel.toggleEditing = 1;
			break;

		case KBPRS_SPACE:

			pKdbgerUiProperty->kdbgerDumpPanel.toggleBits = !pKdbgerUiProperty->kdbgerDumpPanel.toggleBits;
			break;

		default:
			break;
	}
}


