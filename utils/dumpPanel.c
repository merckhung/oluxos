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
		GREEN_BLUE,
		KDBGER_DUMP_TOP_BAR );

	// Print Rtop bar
	if( pKdbgerUiProperty->kdbgerHwFunc != KHF_PCI )
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
		GREEN_BLUE,
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
	u8 *dataPtr, *pDataPtr;
	kdbgerPciDev_t *pKdbgerPciDev;
	kdbgerPciConfig_t *pKdbgerPciConfig;
	s8 *mem = "Mem", *io = "I/O";

	switch( pKdbgerUiProperty->kdbgerHwFunc ) {

		case KHF_PCI:
			dataPtr = (u8 *)&pKdbgerUiProperty->pKdbgerCommPkt->kdbgerRspPciReadPkt.pciContent;
			break;

		case KHF_IO:
			dataPtr = (u8 *)&pKdbgerUiProperty->pKdbgerCommPkt->kdbgerRspIoReadPkt.ioContent;
			break;

		case KHF_IDE:
			dataPtr = (u8 *)&pKdbgerUiProperty->pKdbgerCommPkt->kdbgerRspIdeReadPkt.ideContent;
			break;

		case KHF_CMOS:
			dataPtr = (u8 *)&pKdbgerUiProperty->pKdbgerCommPkt->kdbgerRspCmosReadPkt.cmosContent;
			break;

		default:
		case KHF_MEM:
			dataPtr = (u8 *)&pKdbgerUiProperty->pKdbgerCommPkt->kdbgerRspMemReadPkt.memContent;
		break;
	}
	pDataPtr = dataPtr;

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

		if( pKdbgerUiProperty->kdbgerHwFunc != KHF_PCI )
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

	if( pKdbgerUiProperty->kdbgerHwFunc == KHF_PCI ) {

		pKdbgerPciConfig = (kdbgerPciConfig_t *)pDataPtr;
		sprintf( (s8 *)ap,
			"VEN ID: %4.4Xh\n"
			"DEV ID: %4.4Xh\n\n"
			"Rev ID  : %2.2Xh\n"
			"Int Line: %2.2Xh\n"
			"Int Pin : %2.2Xh\n\n"
			"%s: %8.8Xh\n"
			"%s: %8.8Xh\n"
			"%s: %8.8Xh\n"
			"%s: %8.8Xh\n"
			"%s: %8.8Xh\n"
			"%s: %8.8Xh\n\n"
			"ROM: %8.8Xh\n",
			pKdbgerPciConfig->vendorId,
			pKdbgerPciConfig->deviceId,
			pKdbgerPciConfig->revisionId,
			pKdbgerPciConfig->intLine,
			pKdbgerPciConfig->intPin,

			(pKdbgerPciConfig->baseAddrReg0 & KDBGER_PCIBAR_IO) ? io : mem,
			(pKdbgerPciConfig->baseAddrReg0 & KDBGER_PCIBAR_IO) ?
				(pKdbgerPciConfig->baseAddrReg0 & KDBGER_PCIBAR_IOBA_MASK) :
				(pKdbgerPciConfig->baseAddrReg0 & KDBGER_PCIBAR_MEMBA_MASK),

			(pKdbgerPciConfig->baseAddrReg1 & KDBGER_PCIBAR_IO) ? io : mem,
            (pKdbgerPciConfig->baseAddrReg1 & KDBGER_PCIBAR_IO) ?
                (pKdbgerPciConfig->baseAddrReg1 & KDBGER_PCIBAR_IOBA_MASK) :
                (pKdbgerPciConfig->baseAddrReg1 & KDBGER_PCIBAR_MEMBA_MASK),

			(pKdbgerPciConfig->baseAddrReg2 & KDBGER_PCIBAR_IO) ? io : mem,
            (pKdbgerPciConfig->baseAddrReg2 & KDBGER_PCIBAR_IO) ?
                (pKdbgerPciConfig->baseAddrReg2 & KDBGER_PCIBAR_IOBA_MASK) :
                (pKdbgerPciConfig->baseAddrReg2 & KDBGER_PCIBAR_MEMBA_MASK),

			(pKdbgerPciConfig->baseAddrReg3 & KDBGER_PCIBAR_IO) ? io : mem,
            (pKdbgerPciConfig->baseAddrReg3 & KDBGER_PCIBAR_IO) ?
                (pKdbgerPciConfig->baseAddrReg3 & KDBGER_PCIBAR_IOBA_MASK) :
                (pKdbgerPciConfig->baseAddrReg3 & KDBGER_PCIBAR_MEMBA_MASK),

			(pKdbgerPciConfig->baseAddrReg4 & KDBGER_PCIBAR_IO) ? io : mem,
            (pKdbgerPciConfig->baseAddrReg4 & KDBGER_PCIBAR_IO) ?
                (pKdbgerPciConfig->baseAddrReg4 & KDBGER_PCIBAR_IOBA_MASK) :
                (pKdbgerPciConfig->baseAddrReg4 & KDBGER_PCIBAR_MEMBA_MASK),

			(pKdbgerPciConfig->baseAddrReg5 & KDBGER_PCIBAR_IO) ? io : mem,
            (pKdbgerPciConfig->baseAddrReg5 & KDBGER_PCIBAR_IO) ?
                (pKdbgerPciConfig->baseAddrReg5 & KDBGER_PCIBAR_IOBA_MASK) :
                (pKdbgerPciConfig->baseAddrReg5 & KDBGER_PCIBAR_MEMBA_MASK),

			pKdbgerPciConfig->expRomBaseAddr );
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
		YELLOW_BLUE,
		"%4.4X",
		pKdbgerUiProperty->kdbgerDumpPanel.byteOffset );

	// Print base address & First/Second title
	switch( pKdbgerUiProperty->kdbgerHwFunc ) {

		default:
		case KHF_MEM:

			// Base address
			printWindowAt(
				pKdbgerUiProperty->kdbgerDumpPanel,
				baseaddr, 
				KDBGER_STRING_NLINE,
				20,
				KDBGER_DUMP_BASEADDR_LINE,
				strlen( pKdbgerUiProperty->kdbgerDumpPanel.infoStr ),
				WHITE_BLUE,
				KDBGER_INFO_MEMORY_BASE_FMT,
				(u32)(pKdbgerUiProperty->kdbgerDumpPanel.byteBase >> 32),
				(u32)(pKdbgerUiProperty->kdbgerDumpPanel.byteBase & 0xFFFFFFFFULL) );
			break;

		case KHF_IO:

			// Base address
			printWindowAt(
				pKdbgerUiProperty->kdbgerDumpPanel,
				baseaddr, 
				KDBGER_STRING_NLINE,
				5,
				KDBGER_DUMP_BASEADDR_LINE,
				strlen( pKdbgerUiProperty->kdbgerDumpPanel.infoStr ),
				WHITE_BLUE,
				KDBGER_INFO_IO_BASE_FMT,
				(u32)(pKdbgerUiProperty->kdbgerDumpPanel.byteBase & 0x0000FFFFULL) );
			break;

		case KHF_PCI:

			// Base address
			pKdbgerPciDev = getPciDevice( pKdbgerUiProperty, 
							pKdbgerUiProperty->kdbgerDumpPanel.byteBase );
			if( pKdbgerPciDev )
				printWindowAt(
					pKdbgerUiProperty->kdbgerDumpPanel,
					baseaddr, 
					KDBGER_STRING_NLINE,
					29,
					KDBGER_DUMP_BASEADDR_LINE,
					strlen( pKdbgerUiProperty->kdbgerDumpPanel.infoStr ),
					WHITE_BLUE,
					KDBGER_INFO_PCI_BASE_FMT,
					pKdbgerPciDev->bus, pKdbgerPciDev->dev, pKdbgerPciDev->fun );

			// PCI first/second title
			printWindowMove(
				pKdbgerUiProperty->kdbgerDumpPanel,
				ftitle, 
				KDBGER_STRING_NLINE,
				KDBGER_MAX_PCINAME,
				KDBGER_DUMP_FTITLE_LINE,
				KDBGER_DUMP_FTITLE_COLUMN,
				WHITE_BLUE,
				"%s: %s",
				KDBGER_FTITLE_PCI,
				(pKdbgerUiProperty->pKdbgerPciIds + pKdbgerUiProperty->kdbgerDumpPanel.byteBase)->venTxt );

			printWindowMove(
				pKdbgerUiProperty->kdbgerDumpPanel,
				stitle, 
				KDBGER_STRING_NLINE,
				KDBGER_MAX_PCINAME,
				KDBGER_DUMP_STITLE_LINE,
				KDBGER_DUMP_FTITLE_COLUMN,
				WHITE_BLUE,
				"%s: %s",
				KDBGER_STITLE_PCI,
				(pKdbgerUiProperty->pKdbgerPciIds + pKdbgerUiProperty->kdbgerDumpPanel.byteBase)->devTxt );
			break;

		case KHF_PCIL:
			break;

		case KHF_IDE:

			// Base address
			printWindowAt(
				pKdbgerUiProperty->kdbgerDumpPanel,
				baseaddr, 
				KDBGER_STRING_NLINE,
				20,
				KDBGER_DUMP_BASEADDR_LINE,
				strlen( pKdbgerUiProperty->kdbgerDumpPanel.infoStr ),
				WHITE_BLUE,
				KDBGER_INFO_IDE_BASE_FMT,
				(u32)(pKdbgerUiProperty->kdbgerDumpPanel.byteBase >> 32),
				(u32)(pKdbgerUiProperty->kdbgerDumpPanel.byteBase & 0xFFFFFFFFULL) );
			break;

		case KHF_CMOS:

			// Base address
			printWindowAt(
				pKdbgerUiProperty->kdbgerDumpPanel,
				baseaddr, 
				KDBGER_STRING_NLINE,
				20,
				KDBGER_DUMP_BASEADDR_LINE,
				strlen( pKdbgerUiProperty->kdbgerDumpPanel.infoStr ),
				WHITE_BLUE,
				KDBGER_INFO_CMOS_BASE_FMT,
				(u8)(pKdbgerUiProperty->kdbgerDumpPanel.byteBase & 0xFFULL) );
			break;
	}


	// Highlight & Editing
	y = (pKdbgerUiProperty->kdbgerDumpPanel.byteOffset / KDBGER_DUMP_BYTE_PER_LINE) + KDBGER_DUMP_VALUE_LINE;
	x = ((pKdbgerUiProperty->kdbgerDumpPanel.byteOffset % KDBGER_DUMP_BYTE_PER_LINE) * 3) + KDBGER_DUMP_VALUE_COLUMN;
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
			*(pDataPtr + pKdbgerUiProperty->kdbgerDumpPanel.byteOffset) );

		pKdbgerUiProperty->kdbgerDumpPanel.editingBuf = *(pDataPtr + pKdbgerUiProperty->kdbgerDumpPanel.byteOffset);
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
			OLUX_GET_BIT( *(pDataPtr + pKdbgerUiProperty->kdbgerDumpPanel.byteOffset), 7 ),			
			OLUX_GET_BIT( *(pDataPtr + pKdbgerUiProperty->kdbgerDumpPanel.byteOffset), 6 ),
			OLUX_GET_BIT( *(pDataPtr + pKdbgerUiProperty->kdbgerDumpPanel.byteOffset), 5 ),
			OLUX_GET_BIT( *(pDataPtr + pKdbgerUiProperty->kdbgerDumpPanel.byteOffset), 4 ),
			OLUX_GET_BIT( *(pDataPtr + pKdbgerUiProperty->kdbgerDumpPanel.byteOffset), 3 ),
			OLUX_GET_BIT( *(pDataPtr + pKdbgerUiProperty->kdbgerDumpPanel.byteOffset), 2 ),
			OLUX_GET_BIT( *(pDataPtr + pKdbgerUiProperty->kdbgerDumpPanel.byteOffset), 1 ),
			OLUX_GET_BIT( *(pDataPtr + pKdbgerUiProperty->kdbgerDumpPanel.byteOffset), 0 ) );
	}
	else
		destroyWindow( pKdbgerUiProperty->kdbgerDumpPanel, bits );


	// Print ASCII highlight
	if( pKdbgerUiProperty->kdbgerHwFunc != KHF_PCI ) {
    	y = (pKdbgerUiProperty->kdbgerDumpPanel.byteOffset / KDBGER_DUMP_BYTE_PER_LINE) + KDBGER_DUMP_ASCII_LINE;
    	x = (pKdbgerUiProperty->kdbgerDumpPanel.byteOffset % KDBGER_DUMP_BYTE_PER_LINE) + KDBGER_DUMP_ASCII_COLUMN;
		printWindowMove(
			pKdbgerUiProperty->kdbgerDumpPanel,
			hlascii,
			KDBGER_STRING_NLINE,
			KDBGER_DUMP_HLA_DIGITS,
			y,
			x,
			YELLOW_RED,
			"%c",
			KDBGER_DUMP_ASCII_FILTER( *(pDataPtr + pKdbgerUiProperty->kdbgerDumpPanel.byteOffset) ) );
	}
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
	destroyWindow( pKdbgerUiProperty->kdbgerDumpPanel, hlascii );
	destroyWindow( pKdbgerUiProperty->kdbgerDumpPanel, bits );
	destroyWindow( pKdbgerUiProperty->kdbgerDumpPanel, ftitle );
	destroyWindow( pKdbgerUiProperty->kdbgerDumpPanel, stitle );
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
					writePciByEditing( pKdbgerUiProperty );
					break;

				case KHF_IDE:
					writeIdeByEditing( pKdbgerUiProperty );
					break;

				case KHF_CMOS:
					writeCmosByEditing( pKdbgerUiProperty );
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

			if( (pKdbgerUiProperty->kdbgerDumpPanel.byteOffset - KDBGER_DUMP_BYTE_PER_LINE) >= 0 )
				pKdbgerUiProperty->kdbgerDumpPanel.byteOffset -= KDBGER_DUMP_BYTE_PER_LINE;
			else
				pKdbgerUiProperty->kdbgerDumpPanel.byteOffset = KDBGER_BYTE_PER_SCREEN - (KDBGER_DUMP_BYTE_PER_LINE - pKdbgerUiProperty->kdbgerDumpPanel.byteOffset);
            break;

		case KBPRS_DOWN:

			if( (pKdbgerUiProperty->kdbgerDumpPanel.byteOffset + KDBGER_DUMP_BYTE_PER_LINE) < KDBGER_BYTE_PER_SCREEN )
				pKdbgerUiProperty->kdbgerDumpPanel.byteOffset += KDBGER_DUMP_BYTE_PER_LINE;
			else
				pKdbgerUiProperty->kdbgerDumpPanel.byteOffset %= KDBGER_DUMP_BYTE_PER_LINE;
            break;

		case KBPRS_LEFT:

			if( (pKdbgerUiProperty->kdbgerDumpPanel.byteOffset - 1) >= 0 && (pKdbgerUiProperty->kdbgerDumpPanel.byteOffset % KDBGER_DUMP_BYTE_PER_LINE) > 0 )
				pKdbgerUiProperty->kdbgerDumpPanel.byteOffset--;
			else
				pKdbgerUiProperty->kdbgerDumpPanel.byteOffset += (KDBGER_DUMP_BYTE_PER_LINE - 1);
            break;

		case KBPRS_RIGHT:

			if( (pKdbgerUiProperty->kdbgerDumpPanel.byteOffset + 1) < KDBGER_BYTE_PER_SCREEN 
				&& ((pKdbgerUiProperty->kdbgerDumpPanel.byteOffset + 1) % KDBGER_DUMP_BYTE_PER_LINE) )
				pKdbgerUiProperty->kdbgerDumpPanel.byteOffset++;
			else
				pKdbgerUiProperty->kdbgerDumpPanel.byteOffset -= (KDBGER_DUMP_BYTE_PER_LINE - 1);
            break;

		case KBPRS_PGUP:

			switch( pKdbgerUiProperty->kdbgerHwFunc ) {

				case KHF_PCI:
					if( !pKdbgerUiProperty->kdbgerDumpPanel.byteBase )
						pKdbgerUiProperty->kdbgerDumpPanel.byteBase = pKdbgerUiProperty->numOfPciDevice - 1;
					else
						pKdbgerUiProperty->kdbgerDumpPanel.byteBase--;
					break;

				case KHF_MEM:
					pKdbgerUiProperty->kdbgerDumpPanel.byteBase -= KDBGER_BYTE_PER_SCREEN;
					if( pKdbgerUiProperty->kdbgerDumpPanel.byteBase >= KDBGER_MAXADDR_MEM )
						pKdbgerUiProperty->kdbgerDumpPanel.byteBase =
							(pKdbgerUiProperty->kdbgerDumpPanel.byteBase & 0xFFULL)
							| (KDBGER_MAXADDR_MEM & ~0xFFULL);
					break;

				case KHF_IO:
					pKdbgerUiProperty->kdbgerDumpPanel.byteBase -= KDBGER_BYTE_PER_SCREEN;
					if( pKdbgerUiProperty->kdbgerDumpPanel.byteBase >= KDBGER_MAXADDR_IO )
						pKdbgerUiProperty->kdbgerDumpPanel.byteBase =
							(pKdbgerUiProperty->kdbgerDumpPanel.byteBase & 0xFFULL)
							| (KDBGER_MAXADDR_IO & ~0xFFULL);
					break;

				default:
					break;
			}
				pKdbgerUiProperty->kdbgerDumpPanel.byteBase -= KDBGER_BYTE_PER_SCREEN;
            break;

		case KBPRS_PGDN:

			switch( pKdbgerUiProperty->kdbgerHwFunc ) {

				case KHF_PCI:
					if( (pKdbgerUiProperty->kdbgerDumpPanel.byteBase + 1) 
						>= pKdbgerUiProperty->numOfPciDevice )
						pKdbgerUiProperty->kdbgerDumpPanel.byteBase = 0;
					else
						pKdbgerUiProperty->kdbgerDumpPanel.byteBase++;
					break;

				case KHF_MEM:
					pKdbgerUiProperty->kdbgerDumpPanel.byteBase += KDBGER_BYTE_PER_SCREEN;
					if( pKdbgerUiProperty->kdbgerDumpPanel.byteBase >= KDBGER_MAXADDR_MEM )
						pKdbgerUiProperty->kdbgerDumpPanel.byteBase &= 0xFFULL;
					break;

				case KHF_IO:
					pKdbgerUiProperty->kdbgerDumpPanel.byteBase += KDBGER_BYTE_PER_SCREEN;
					if( pKdbgerUiProperty->kdbgerDumpPanel.byteBase >= KDBGER_MAXADDR_IO )
						pKdbgerUiProperty->kdbgerDumpPanel.byteBase &= 0xFFULL;
					break;

				default:
					break;
			}
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


