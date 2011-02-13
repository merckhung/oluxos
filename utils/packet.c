/*
 * Copyright (C) 2011 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * File: packet.c
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
			pKdbgerUiProperty->kdbgerDumpPanel.dumpByteBase,
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
			pKdbgerUiProperty->kdbgerDumpPanel.dumpByteBase + pKdbgerUiProperty->kdbgerDumpPanel.dumpByteOffset,
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
			pKdbgerUiProperty->kdbgerDumpPanel.dumpByteBase,
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
			pKdbgerUiProperty->kdbgerDumpPanel.dumpByteBase + pKdbgerUiProperty->kdbgerDumpPanel.dumpByteOffset,
			sizeof( pKdbgerUiProperty->kdbgerDumpPanel.editingBuf ),
			&pKdbgerUiProperty->kdbgerDumpPanel.editingBuf,
			pKdbgerUiProperty->pktBuf,
			KDBGER_MAXSZ_PKT );
}


