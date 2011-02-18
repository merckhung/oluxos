/*
 * Copyright (C) 2011 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * File: utils.c
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
		return 1;

	// Save PCI list
	pKdbgerRspPciListPkt = (kdbgerRspPciListPkt_t *)pKdbgerUiProperty->pktBuf;
	pKdbgerUiProperty->numOfPciDevice = pKdbgerRspPciListPkt->numOfPciDevice;
	pKdbgerUiProperty->pKdbgerPciDev = (kdbgerPciDev_t *)malloc( sizeof( kdbgerPciDev_t ) * pKdbgerUiProperty->numOfPciDevice );

	if( !pKdbgerUiProperty->pKdbgerPciDev )
		return 1;

	memcpy( pKdbgerUiProperty->pKdbgerPciDev, &pKdbgerRspPciListPkt->pciListContent, sizeof( kdbgerPciDev_t ) * pKdbgerUiProperty->numOfPciDevice );

	return 0;
}


kdbgerPciDev_t *getPciDevice( kdbgerUiProperty_t *pKdbgerUiProperty, s32 num ) {

	if( num >= pKdbgerUiProperty->numOfPciDevice )
		return NULL;

	return pKdbgerUiProperty->pKdbgerPciDev + num;
}


s32 readE820List( kdbgerUiProperty_t *pKdbgerUiProperty ) {

	kdbgerRspE820ListPkt_t *pKdbgerRspE820ListPkt;

	// Read E820 list
	if( executeFunction( pKdbgerUiProperty->fd, KDBGER_REQ_E810_LIST, 0, 0, NULL, pKdbgerUiProperty->pktBuf, KDBGER_MAXSZ_PKT ) )
		return 1;

	// Save E820 list
	pKdbgerRspE820ListPkt = (kdbgerRspE820ListPkt_t *)pKdbgerUiProperty->pktBuf;
	pKdbgerUiProperty->numOfE820Record = pKdbgerRspE820ListPkt->numOfE820Record;
	pKdbgerUiProperty->pKdbgerE820record = (kdbgerE820record_t *)malloc( sizeof( kdbgerE820record_t ) * pKdbgerUiProperty->numOfE820Record );

	if( !pKdbgerUiProperty->pKdbgerE820record )
		return 1;

	memcpy( pKdbgerUiProperty->pKdbgerE820record, &pKdbgerRspE820ListPkt->e820ListContent, sizeof( kdbgerE820record_t ) * pKdbgerUiProperty->numOfE820Record );

	return 0;
}


s32 readMemory( kdbgerUiProperty_t *pKdbgerUiProperty ) {

	// Read memory
	return executeFunction(
			pKdbgerUiProperty->fd,
			KDBGER_REQ_MEM_READ,
			pKdbgerUiProperty->kdbgerDumpPanel.byteBase,
			KDBGER_BYTE_PER_SCREEN,
			NULL,
			pKdbgerUiProperty->pktBuf,
			KDBGER_MAXSZ_PKT );
}


s32 writeMemoryByEditing( kdbgerUiProperty_t *pKdbgerUiProperty ) {

	// Write memory
	return executeFunction(
			pKdbgerUiProperty->fd,
			KDBGER_REQ_MEM_WRITE,
			pKdbgerUiProperty->kdbgerDumpPanel.byteBase + pKdbgerUiProperty->kdbgerDumpPanel.byteOffset,
			sizeof( pKdbgerUiProperty->kdbgerDumpPanel.editingBuf ),
			&pKdbgerUiProperty->kdbgerDumpPanel.editingBuf,
			pKdbgerUiProperty->pktBuf,
			KDBGER_MAXSZ_PKT );
}


s32 readIo( kdbgerUiProperty_t *pKdbgerUiProperty ) {

	// Read io
	return executeFunction(
			pKdbgerUiProperty->fd,
			KDBGER_REQ_IO_READ,
			pKdbgerUiProperty->kdbgerDumpPanel.byteBase,
			KDBGER_BYTE_PER_SCREEN,
			NULL,
			pKdbgerUiProperty->pktBuf,
			KDBGER_MAXSZ_PKT );
}


s32 writeIoByEditing( kdbgerUiProperty_t *pKdbgerUiProperty ) {

	// Write io
	return executeFunction(
			pKdbgerUiProperty->fd,
			KDBGER_REQ_IO_WRITE,
			pKdbgerUiProperty->kdbgerDumpPanel.byteBase + pKdbgerUiProperty->kdbgerDumpPanel.byteOffset,
			sizeof( pKdbgerUiProperty->kdbgerDumpPanel.editingBuf ),
			&pKdbgerUiProperty->kdbgerDumpPanel.editingBuf,
			pKdbgerUiProperty->pktBuf,
			KDBGER_MAXSZ_PKT );
}


s32 readPci( kdbgerUiProperty_t *pKdbgerUiProperty ) {

	kdbgerPciDev_t *pKdbgerPciDev;

	// Get PCI device
	pKdbgerPciDev = getPciDevice( 
		pKdbgerUiProperty, 
		pKdbgerUiProperty->kdbgerDumpPanel.byteBase );
	if( !pKdbgerPciDev )
		return 1;

	// Read PCI
	return executeFunction(
			pKdbgerUiProperty->fd,
			KDBGER_REQ_PCI_READ,
			calculatePciAddress( 
				pKdbgerPciDev->bus, 
				pKdbgerPciDev->dev, 
				pKdbgerPciDev->fun ),
			KDBGER_BYTE_PER_SCREEN,
			NULL,
			pKdbgerUiProperty->pktBuf,
			KDBGER_MAXSZ_PKT );
}


s32 writePciByEditing( kdbgerUiProperty_t *pKdbgerUiProperty ) {

	kdbgerPciDev_t *pKdbgerPciDev;

    // Get PCI device
	pKdbgerPciDev = getPciDevice(
		pKdbgerUiProperty,
		pKdbgerUiProperty->kdbgerDumpPanel.byteBase );
	if( !pKdbgerPciDev )
		return 1;

	// Write PCI
	return executeFunction(
			pKdbgerUiProperty->fd,
			KDBGER_REQ_PCI_WRITE,
			calculatePciAddress(
				pKdbgerPciDev->bus,
				pKdbgerPciDev->dev,
				pKdbgerPciDev->fun ),
			sizeof( pKdbgerUiProperty->kdbgerDumpPanel.editingBuf ),
			&pKdbgerUiProperty->kdbgerDumpPanel.editingBuf,
			pKdbgerUiProperty->pktBuf,
			KDBGER_MAXSZ_PKT );
}


u32 calculatePciAddress( u16 bus, u8 dev, u8 func ) {

	return 0x80000000 
		| (((u32)bus) << 16) 
		| ((((u32)dev) & 0x1F) << 11) 
		| ((((u32)func) & 0x07) << 8);
}


