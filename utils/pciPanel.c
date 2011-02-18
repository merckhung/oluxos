/*
 * Copyright (C) 2011 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * File: pciPanel.c
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


void printPciBasePanel( kdbgerUiProperty_t *pKdbgerUiProperty ) {

	// Print Top bar
	printWindowAt( pKdbgerUiProperty->kdbgerPciPanel,
		top, 
		KDBGER_STRING_NLINE,
		strlen( KDBGER_DUMP_TOP_BAR ),
		KDBGER_DUMP_TOP_LINE,
		KDBGER_DUMP_TOP_COLUMN,
		GREEN_BLUE,
		KDBGER_DUMP_TOP_BAR );

	// Print Rtop bar
	printWindowAt( pKdbgerUiProperty->kdbgerPciPanel,
		rtop, 
		KDBGER_STRING_NLINE,
		KDBGER_DUMP_BYTE_PER_LINE,
		KDBGER_DUMP_RTOP_LINE,
		KDBGER_DUMP_RTOP_COLUMN,
		RED_BLUE,
		KDBGER_DUMP_RTOP_BAR );

	// Print Left bar
	printWindowAt( pKdbgerUiProperty->kdbgerPciPanel,
		left,
		KDBGER_DUMP_BYTE_PER_LINE,
		4,
		KDBGER_DUMP_LEFT_LINE,
		KDBGER_DUMP_LEFT_COLUMN,
		GREEN_BLUE,
		KDBGER_DUMP_LEFT_BAR );

	// Print Info base
	printWindowAt( pKdbgerUiProperty->kdbgerPciPanel,
		info,
		KDBGER_STRING_NLINE,
		KDBGER_MAX_COLUMN,
		KDBGER_INFO_LINE,
		KDBGER_INFO_COLUMN,
		WHITE_BLUE,
		"%s",
		pKdbgerUiProperty->kdbgerPciPanel.infoStr );
}


void printPciUpdatePanel( kdbgerUiProperty_t *pKdbgerUiProperty ) {

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
		pKdbgerUiProperty->kdbgerPciPanel,
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
		pKdbgerUiProperty->kdbgerPciPanel,
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
		pKdbgerUiProperty->kdbgerPciPanel,
		offset, 
		KDBGER_STRING_NLINE,
		4,
		KDBGER_DUMP_OFF_LINE,
		KDBGER_DUMP_OFF_COLUMN,
		YELLOW_BLUE,
		"%4.4X",
		pKdbgerUiProperty->kdbgerPciPanel.byteOffset );

	// Print base address
	switch( pKdbgerUiProperty->kdbgerHwFunc ) {

		case KHF_MEM:
			printWindowAt(
				pKdbgerUiProperty->kdbgerPciPanel,
				baseaddr, 
				KDBGER_STRING_NLINE,
				20,
				KDBGER_DUMP_BASEADDR_LINE,
				strlen( pKdbgerUiProperty->kdbgerPciPanel.infoStr ),
				WHITE_BLUE,
				KDBGER_INFO_MEMORY_BASE_FMT,
				(u32)(pKdbgerUiProperty->kdbgerPciPanel.byteBase >> 32),
				(u32)(pKdbgerUiProperty->kdbgerPciPanel.byteBase & 0xFFFFFFFFULL) );
			break;

		case KHF_IO:
			printWindowAt(
				pKdbgerUiProperty->kdbgerPciPanel,
				baseaddr, 
				KDBGER_STRING_NLINE,
				20,
				KDBGER_DUMP_BASEADDR_LINE,
				strlen( pKdbgerUiProperty->kdbgerPciPanel.infoStr ),
				WHITE_BLUE,
				KDBGER_INFO_IO_BASE_FMT,
				(u32)(pKdbgerUiProperty->kdbgerPciPanel.byteBase & 0x0000FFFFULL) );
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
	y = (pKdbgerUiProperty->kdbgerPciPanel.byteOffset / KDBGER_DUMP_BYTE_PER_LINE) + KDBGER_DUMP_VALUE_LINE;
	x = ((pKdbgerUiProperty->kdbgerPciPanel.byteOffset % KDBGER_DUMP_BYTE_PER_LINE) * 3) + KDBGER_DUMP_VALUE_COLUMN;
	if( pKdbgerUiProperty->kdbgerPciPanel.toggleEditing ) {

		color = (editorColorCount++ % 2) ? YELLOW_RED : YELLOW_BLACK;

		printWindowMove(
			pKdbgerUiProperty->kdbgerPciPanel,
			highlight,
			KDBGER_STRING_NLINE,
			KDBGER_DUMP_HL_DIGITS,
			y,
			x,
			color,
			"%2.2X",
			pKdbgerUiProperty->kdbgerPciPanel.editingBuf );
		}
	else {

		printWindowMove(
			pKdbgerUiProperty->kdbgerPciPanel,
			highlight,
			KDBGER_STRING_NLINE,
			KDBGER_DUMP_HL_DIGITS,
			y,
			x,
			YELLOW_RED,
			"%2.2X",
			*(pDataPtr + pKdbgerUiProperty->kdbgerPciPanel.byteOffset) );

		pKdbgerUiProperty->kdbgerPciPanel.editingBuf = *(pDataPtr + pKdbgerUiProperty->kdbgerPciPanel.byteOffset);
	}


	// Bits
	if( pKdbgerUiProperty->kdbgerPciPanel.toggleBits ) {

		printWindowMove(
			pKdbgerUiProperty->kdbgerPciPanel,
			bits,
			KDBGER_STRING_NLINE,
			KDBGER_DUMP_BITS_DIGITS,
			y + 1,
			x,
			WHITE_RED,
			"%d%d%d%d_%d%d%d%d",
			OLUX_GET_BIT( *(pDataPtr + pKdbgerUiProperty->kdbgerPciPanel.byteOffset), 7 ),			
			OLUX_GET_BIT( *(pDataPtr + pKdbgerUiProperty->kdbgerPciPanel.byteOffset), 6 ),
			OLUX_GET_BIT( *(pDataPtr + pKdbgerUiProperty->kdbgerPciPanel.byteOffset), 5 ),
			OLUX_GET_BIT( *(pDataPtr + pKdbgerUiProperty->kdbgerPciPanel.byteOffset), 4 ),
			OLUX_GET_BIT( *(pDataPtr + pKdbgerUiProperty->kdbgerPciPanel.byteOffset), 3 ),
			OLUX_GET_BIT( *(pDataPtr + pKdbgerUiProperty->kdbgerPciPanel.byteOffset), 2 ),
			OLUX_GET_BIT( *(pDataPtr + pKdbgerUiProperty->kdbgerPciPanel.byteOffset), 1 ),
			OLUX_GET_BIT( *(pDataPtr + pKdbgerUiProperty->kdbgerPciPanel.byteOffset), 0 ) );
	}
	else
		destroyWindow( pKdbgerUiProperty->kdbgerPciPanel, bits );


	// Print ASCII highlight
    y = (pKdbgerUiProperty->kdbgerPciPanel.byteOffset / KDBGER_DUMP_BYTE_PER_LINE) + KDBGER_DUMP_ASCII_LINE;
    x = (pKdbgerUiProperty->kdbgerPciPanel.byteOffset % KDBGER_DUMP_BYTE_PER_LINE) + KDBGER_DUMP_ASCII_COLUMN;
	printWindowMove(
		pKdbgerUiProperty->kdbgerPciPanel,
		hlascii,
		KDBGER_STRING_NLINE,
		KDBGER_DUMP_HLA_DIGITS,
		y,
		x,
		YELLOW_RED,
		"%c",
		KDBGER_DUMP_ASCII_FILTER( *(pDataPtr + pKdbgerUiProperty->kdbgerPciPanel.byteOffset) ) );
}


void clearPciBasePanel( kdbgerUiProperty_t *pKdbgerUiProperty ) {

	destroyWindow( pKdbgerUiProperty->kdbgerPciPanel, top );
	destroyWindow( pKdbgerUiProperty->kdbgerPciPanel, rtop );
	destroyWindow( pKdbgerUiProperty->kdbgerPciPanel, left );
	destroyWindow( pKdbgerUiProperty->kdbgerPciPanel, info );
}


void clearPciUpdatePanel( kdbgerUiProperty_t *pKdbgerUiProperty ) {

	destroyWindow( pKdbgerUiProperty->kdbgerPciPanel, value );
	destroyWindow( pKdbgerUiProperty->kdbgerPciPanel, ascii );
	destroyWindow( pKdbgerUiProperty->kdbgerPciPanel, offset );
	destroyWindow( pKdbgerUiProperty->kdbgerPciPanel, baseaddr );
	destroyWindow( pKdbgerUiProperty->kdbgerPciPanel, highlight );
	destroyWindow( pKdbgerUiProperty->kdbgerPciPanel, hlascii );
	destroyWindow( pKdbgerUiProperty->kdbgerPciPanel, bits );
}


void handleKeyPressForPciPanel( kdbgerUiProperty_t *pKdbgerUiProperty ) {

	if( pKdbgerUiProperty->kdbgerPciPanel.toggleEditing ) {

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
			pKdbgerUiProperty->kdbgerPciPanel.toggleEditing = 0;
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
			pKdbgerUiProperty->kdbgerPciPanel.editingBuf <<= 4;

			if( pKdbgerUiProperty->inputBuf <= '9' ) {

				pKdbgerUiProperty->kdbgerPciPanel.editingBuf |=
					(u8)((pKdbgerUiProperty->inputBuf - 0x30) & 0x0F);
			}
			else if( pKdbgerUiProperty->inputBuf > 'F' ) {

				pKdbgerUiProperty->kdbgerPciPanel.editingBuf |=
					(u8)((pKdbgerUiProperty->inputBuf - 0x60 + 9) & 0x0F);
			}
			else {

				pKdbgerUiProperty->kdbgerPciPanel.editingBuf |=
					(u8)((pKdbgerUiProperty->inputBuf - 0x40 + 9) & 0x0F);
			}
		}

		return;
	}

	switch( pKdbgerUiProperty->inputBuf ) {

		case KBPRS_UP:

			if( (pKdbgerUiProperty->kdbgerPciPanel.byteOffset - KDBGER_DUMP_BYTE_PER_LINE) >= 0 )
				pKdbgerUiProperty->kdbgerPciPanel.byteOffset -= KDBGER_DUMP_BYTE_PER_LINE;
			else
				pKdbgerUiProperty->kdbgerPciPanel.byteOffset = KDBGER_BYTE_PER_SCREEN - (KDBGER_DUMP_BYTE_PER_LINE - pKdbgerUiProperty->kdbgerPciPanel.byteOffset);
            break;

		case KBPRS_DOWN:

			if( (pKdbgerUiProperty->kdbgerPciPanel.byteOffset + KDBGER_DUMP_BYTE_PER_LINE) < KDBGER_BYTE_PER_SCREEN )
				pKdbgerUiProperty->kdbgerPciPanel.byteOffset += KDBGER_DUMP_BYTE_PER_LINE;
			else
				pKdbgerUiProperty->kdbgerPciPanel.byteOffset %= KDBGER_DUMP_BYTE_PER_LINE;
            break;

		case KBPRS_LEFT:

			if( (pKdbgerUiProperty->kdbgerPciPanel.byteOffset - 1) >= 0 && (pKdbgerUiProperty->kdbgerPciPanel.byteOffset % KDBGER_DUMP_BYTE_PER_LINE) > 0 )
				pKdbgerUiProperty->kdbgerPciPanel.byteOffset--;
			else
				pKdbgerUiProperty->kdbgerPciPanel.byteOffset += (KDBGER_DUMP_BYTE_PER_LINE - 1);
            break;

		case KBPRS_RIGHT:

			if( (pKdbgerUiProperty->kdbgerPciPanel.byteOffset + 1) < KDBGER_BYTE_PER_SCREEN 
				&& ((pKdbgerUiProperty->kdbgerPciPanel.byteOffset + 1) % KDBGER_DUMP_BYTE_PER_LINE) )
				pKdbgerUiProperty->kdbgerPciPanel.byteOffset++;
			else
				pKdbgerUiProperty->kdbgerPciPanel.byteOffset -= (KDBGER_DUMP_BYTE_PER_LINE - 1);
            break;

		case KBPRS_PGUP:

			pKdbgerUiProperty->kdbgerPciPanel.byteBase -= KDBGER_BYTE_PER_SCREEN;
            break;

		case KBPRS_PGDN:

			pKdbgerUiProperty->kdbgerPciPanel.byteBase += KDBGER_BYTE_PER_SCREEN;
            break;

		case KBPRS_ENTER:

			pKdbgerUiProperty->kdbgerPciPanel.toggleEditing = 1;
			break;

		case KBPRS_SPACE:

			pKdbgerUiProperty->kdbgerPciPanel.toggleBits = !pKdbgerUiProperty->kdbgerPciPanel.toggleBits;
			break;

		default:
			break;
	}
}


