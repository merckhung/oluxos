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

		case KDBGER_RSP_CMOS_READ:
            if( op != KDBGER_REQ_CMOS_READ )
                return 1;
			break;

		case KDBGER_RSP_CMOS_WRITE:
            if( op != KDBGER_REQ_CMOS_WRITE )
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

		case KDBGER_REQ_CMOS_READ:

			pKdbgerCommPkt->kdbgerReqCmosReadPkt.address = (u8)(addr & 0xFFULL);
			pKdbgerCommPkt->kdbgerReqCmosReadPkt.size = (u8)(size & 0xFF);
			pKdbgerCommPkt->kdbgerCommHdr.pktLen = sizeof( kdbgerReqCmosReadPkt_t );
			break;

		case KDBGER_REQ_CMOS_WRITE:

			if( !size || !cntBuf )
				return 1;

			pKdbgerCommPkt->kdbgerReqCmosWritePkt.address = (u8)(addr & 0xFFULL);
			pKdbgerCommPkt->kdbgerReqCmosWritePkt.size = (u8)(size & 0xFF);
			memcpy( &pKdbgerCommPkt->kdbgerReqCmosWritePkt.cmosContent, cntBuf, size );
			pKdbgerCommPkt->kdbgerCommHdr.pktLen = 
				sizeof( kdbgerReqCmosWritePkt_t ) - sizeof( s8 * ) + size; 
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


