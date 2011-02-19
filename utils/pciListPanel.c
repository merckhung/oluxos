/*
 * Copyright (C) 2011 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * File: pciListPanel.c
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


void printPciListBasePanel( kdbgerUiProperty_t *pKdbgerUiProperty ) {

	// Title
	printWindowAt( pKdbgerUiProperty->kdbgerPciListPanel,
		title, 
		KDBGER_PCIL_TITLE_LINE,
		KDBGER_PCIL_TITLE_COLUMN,
		KDBGER_PCIL_TITLE_X_POS,
		KDBGER_PCIL_TITLE_Y_POS,
		YELLOW_BLACK,
		KDBGER_PCIL_TITLE );
}


void printPciListUpdatePanel( kdbgerUiProperty_t *pKdbgerUiProperty ) {

	s8 *p, buf[ KDBGER_BUF_SIZE ], hlbuf[ KDBGER_PCIL_CON_COLUMN + 1 ];
	s32 i;
	s32 start, end;

	// Range
	start = pKdbgerUiProperty->kdbgerPciListPanel.pageOffset;
	end = pKdbgerUiProperty->kdbgerPciListPanel.pageOffset + KDBGER_REC_PER_PAGE;
	if( end > pKdbgerUiProperty->numOfPciDevice )
		end = pKdbgerUiProperty->numOfPciDevice;

	// Prepare data
	p = buf;
	for( i = start ; i < end ; i++ ) {

		// Prepare for the list
		p += snprintf( p, 
			(KDBGER_BUF_SIZE - (p - buf)),
			KDBGER_PCIL_LINE_FMT,
			(pKdbgerUiProperty->pKdbgerPciIds + i)->venTxt,
			(pKdbgerUiProperty->pKdbgerPciIds + i)->devTxt,
			(pKdbgerUiProperty->pKdbgerPciDev + i)->vendorId,
			(pKdbgerUiProperty->pKdbgerPciDev + i)->deviceId,
			(pKdbgerUiProperty->pKdbgerPciDev + i)->bus,
			(pKdbgerUiProperty->pKdbgerPciDev + i)->dev,
			(pKdbgerUiProperty->pKdbgerPciDev + i)->fun );

		// Prepare for the highlight bar
		if( i == (pKdbgerUiProperty->kdbgerPciListPanel.hlIndex + pKdbgerUiProperty->kdbgerPciListPanel.pageOffset) )
			snprintf( hlbuf,
				KDBGER_PCIL_CON_COLUMN + 1,
				KDBGER_PCIL_LINE_FMT,
				(pKdbgerUiProperty->pKdbgerPciIds + i)->venTxt,
				(pKdbgerUiProperty->pKdbgerPciIds + i)->devTxt,
				(pKdbgerUiProperty->pKdbgerPciDev + i)->vendorId,
				(pKdbgerUiProperty->pKdbgerPciDev + i)->deviceId,
				(pKdbgerUiProperty->pKdbgerPciDev + i)->bus,
				(pKdbgerUiProperty->pKdbgerPciDev + i)->dev,
				(pKdbgerUiProperty->pKdbgerPciDev + i)->fun );
	}

	// Update the screen
	printWindowAt( pKdbgerUiProperty->kdbgerPciListPanel,
		content, 
		KDBGER_PCIL_CON_LINE,
		KDBGER_PCIL_CON_COLUMN,
		KDBGER_PCIL_CON_X_POS,
		KDBGER_PCIL_CON_Y_POS,
		WHITE_BLUE,
		"%s",
		buf );

	// Highlight
	printWindowMove( pKdbgerUiProperty->kdbgerPciListPanel,
		highlight, 
		KDBGER_STRING_NLINE,
		KDBGER_PCIL_CON_COLUMN,
		KDBGER_PCIL_CON_X_POS + pKdbgerUiProperty->kdbgerPciListPanel.hlIndex,
		KDBGER_PCIL_CON_Y_POS,
		BLACK_CYAN,
		"%s",
		hlbuf );
}


void clearPciListBasePanel( kdbgerUiProperty_t *pKdbgerUiProperty ) {

	destroyWindow( pKdbgerUiProperty->kdbgerPciListPanel, title );
}


void clearPciListUpdatePanel( kdbgerUiProperty_t *pKdbgerUiProperty ) {

	destroyWindow( pKdbgerUiProperty->kdbgerPciListPanel, content );
	destroyWindow( pKdbgerUiProperty->kdbgerPciListPanel, highlight );
}


s32 handleKeyPressForPciListPanel( kdbgerUiProperty_t *pKdbgerUiProperty ) {

	switch( pKdbgerUiProperty->inputBuf ) {

		case KBPRS_UP:

			if( pKdbgerUiProperty->kdbgerPciListPanel.hlIndex )
				pKdbgerUiProperty->kdbgerPciListPanel.hlIndex--;
			else
				if( pKdbgerUiProperty->kdbgerPciListPanel.pageOffset )
					pKdbgerUiProperty->kdbgerPciListPanel.pageOffset--;
            break;

		case KBPRS_DOWN:

			if( pKdbgerUiProperty->kdbgerPciListPanel.hlIndex 
					< (KDBGER_REC_PER_PAGE - 1) 
				&& (pKdbgerUiProperty->kdbgerPciListPanel.pageOffset 
					+ pKdbgerUiProperty->kdbgerPciListPanel.hlIndex) 
				< (pKdbgerUiProperty->numOfPciDevice - 1) )
				pKdbgerUiProperty->kdbgerPciListPanel.hlIndex++;
			else
				if( (pKdbgerUiProperty->kdbgerPciListPanel.pageOffset + KDBGER_REC_PER_PAGE)
					< pKdbgerUiProperty->numOfPciDevice )
					pKdbgerUiProperty->kdbgerPciListPanel.pageOffset++;
            break;

		case KBPRS_PGUP:

			if( pKdbgerUiProperty->numOfPciDevice < KDBGER_REC_PER_PAGE )
				break;

			if( pKdbgerUiProperty->kdbgerPciListPanel.pageOffset >= KDBGER_REC_PER_PAGE )
				pKdbgerUiProperty->kdbgerPciListPanel.pageOffset -= KDBGER_REC_PER_PAGE;
			else
				pKdbgerUiProperty->kdbgerPciListPanel.pageOffset = 0;

			pKdbgerUiProperty->kdbgerPciListPanel.hlIndex = 0;
            break;

		case KBPRS_PGDN:

			if( pKdbgerUiProperty->numOfPciDevice < KDBGER_REC_PER_PAGE )
				break;

			if( (pKdbgerUiProperty->kdbgerPciListPanel.pageOffset + KDBGER_REC_PER_PAGE) 
				< pKdbgerUiProperty->numOfPciDevice ) {

				pKdbgerUiProperty->kdbgerPciListPanel.pageOffset += KDBGER_REC_PER_PAGE;
				if( (pKdbgerUiProperty->numOfPciDevice 
					- pKdbgerUiProperty->kdbgerPciListPanel.pageOffset) 
					< KDBGER_REC_PER_PAGE )
					pKdbgerUiProperty->kdbgerPciListPanel.pageOffset =
						pKdbgerUiProperty->numOfPciDevice - KDBGER_REC_PER_PAGE;
			}
			else
				pKdbgerUiProperty->kdbgerPciListPanel.pageOffset =
					pKdbgerUiProperty->numOfPciDevice - KDBGER_REC_PER_PAGE;

			pKdbgerUiProperty->kdbgerPciListPanel.hlIndex = KDBGER_REC_PER_PAGE - 1;
            break;

		case KBPRS_ENTER:
			
			pKdbgerUiProperty->kdbgerDumpPanel.byteBase = 
				pKdbgerUiProperty->kdbgerPciListPanel.hlIndex + 
				pKdbgerUiProperty->kdbgerPciListPanel.pageOffset;
			return 1;

		default:
			break;
	}

	return 0;
}


