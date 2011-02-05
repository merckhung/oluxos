/*
 * Copyright (C) 2006 -  2011 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * File: kdbger.c
 * Description:
 * 	OluxOS IA32 Kernel Debugger
 *
 */
#include <types.h>
#include <clib.h>
#include <ia32/platform.h>
#include <ia32/interrupt.h>
#include <ia32/io.h>
#include <ia32/gdb.h>
#include <ia32/debug.h>
#include <ia32/kdbger.h>


ExternIRQHandler( 4 );


static u16 kdbgerPortAddr = UART_PORT0;
static u8 kdbgerState = KDBGER_UNKNOWN;
static s8 pktBuf[ KDBGER_MAXSZ_PKT ];
static s32 idxBuf;


static void kdbgerSetState( kdbgerState_t state ) {

	kdbgerState = state;
}


static u8 kdbgerGetState( void ) {

	return kdbgerState;
}


static void kdbgerIntHandler( u8 IrqNum ) {

	u8 lsrState;
	kdbgerCommPkt_t *pKdbgerCommPkt = (kdbgerCommPkt_t *)pktBuf;
	s8 *ptr;
	volatile u8 *phyMem;
	u32 i, sz;
	u64 addr;
	u16 ioAddr;

    // Disable interrupt
    IoOutByte( 0x00, kdbgerPortAddr + UART_IER );

	if( kdbgerGetState() == KDBGER_UNKNOWN ) {

		DbgPrint( "Kernel Debugger internal state is unknown\n" );
		goto done;
	}

	// Read LSR
	lsrState = IoInByte( kdbgerPortAddr + UART_LSR );
	if( lsrState & UART_LSR_RXDR ) {

		//DbgPrint( "UART_LSR_RXDR\n" );

		// Receive & assemble a request packet
		if( kdbgerGetState() == KDBGER_READY ) {

			// Transit to RECV state
			kdbgerSetState( KDBGER_PKT_RECV );
			idxBuf = 0;
			CbMemSet( pktBuf, 0, KDBGER_MAXSZ_PKT );
		}
		else if( kdbgerGetState() != KDBGER_PKT_RECV ) {

			// Internal error
			kdbgerSetState( KDBGER_UNKNOWN );
			goto done;
		}

		// Receive one byte of a request packet
		pktBuf[ idxBuf++ ] = IoInByte( kdbgerPortAddr + UART_RBR );

		// Response to a request
		if( (kdbgerGetState() == KDBGER_PKT_RECV) && (idxBuf >= sizeof( kdbgerCommHdr_t ))
			&& (idxBuf >= pKdbgerCommPkt->kdbgerCommHdr.pktLen) ) {

			// Indicate receiving has done
			kdbgerSetState( KDBGER_PKT_DONE );

			// Look at OpCode
			switch( pKdbgerCommPkt->kdbgerCommHdr.opCode ) {

				case KDBGER_REQ_MEM_READ:

					// Read memory content
					ptr = (s8 *)&pKdbgerCommPkt->kdbgerRspMemReadPkt.memContent;
					addr = pKdbgerCommPkt->kdbgerReqMemReadPkt.address;
					phyMem = (volatile u8 *)(u32)addr;
					sz = pKdbgerCommPkt->kdbgerReqMemReadPkt.size;

					CbMemSet( pktBuf, 0, KDBGER_MAXSZ_PKT );
					for( i = 0 ; i < sz ; i++ ) {

						*(ptr + i) = *(phyMem + i);
					}

					// Prepare the response packet
					pKdbgerCommPkt->kdbgerCommHdr.opCode = KDBGER_RSP_MEM_READ;
					pKdbgerCommPkt->kdbgerCommHdr.pktLen = 
						sizeof( kdbgerRspMemReadPkt_t ) - sizeof( s8 * ) + sz;
					pKdbgerCommPkt->kdbgerCommHdr.errorCode = KDBGER_SUCCESS;
					pKdbgerCommPkt->kdbgerRspMemReadPkt.address = addr;
					pKdbgerCommPkt->kdbgerRspMemReadPkt.size = sz;
					break;

				case KDBGER_REQ_MEM_WRITE:

					// write memory content
					ptr = (s8 *)&pKdbgerCommPkt->kdbgerReqMemWritePkt.memContent;
					addr = pKdbgerCommPkt->kdbgerReqMemWritePkt.address;
					phyMem = (volatile u8 *)(u32)addr;
					sz = pKdbgerCommPkt->kdbgerReqMemWritePkt.size;

					for( i = 0 ; i < sz ; i++ ) {

						*(phyMem + i) = *(ptr + i);
					}

					// Prepare the response packet
					pKdbgerCommPkt->kdbgerCommHdr.opCode = KDBGER_RSP_MEM_WRITE;
					pKdbgerCommPkt->kdbgerCommHdr.pktLen = sizeof( kdbgerRspMemWritePkt_t );
					pKdbgerCommPkt->kdbgerCommHdr.errorCode = KDBGER_SUCCESS;
					pKdbgerCommPkt->kdbgerRspMemWritePkt.address = addr;
					pKdbgerCommPkt->kdbgerRspMemWritePkt.size = sz;
					break;

				case KDBGER_REQ_IO_READ:

					// Read IO content
					ptr = (s8 *)&pKdbgerCommPkt->kdbgerRspIoReadPkt.ioContent;
					ioAddr = pKdbgerCommPkt->kdbgerReqIoReadPkt.address;
					sz = pKdbgerCommPkt->kdbgerReqIoReadPkt.size;

					CbMemSet( pktBuf, 0, KDBGER_MAXSZ_PKT );
					for( i = 0 ; i < sz ; i++ ) {

						// Read IO data
						*(ptr + i) = IoInByte( ioAddr + i );
					}

					// Prepare the response packet
					pKdbgerCommPkt->kdbgerCommHdr.opCode = KDBGER_RSP_IO_READ;
					pKdbgerCommPkt->kdbgerCommHdr.pktLen = 
						sizeof( kdbgerRspIoReadPkt_t ) - sizeof( s8 * ) + sz;
					pKdbgerCommPkt->kdbgerCommHdr.errorCode = KDBGER_SUCCESS;
					pKdbgerCommPkt->kdbgerRspIoReadPkt.address = ioAddr;
					pKdbgerCommPkt->kdbgerRspIoReadPkt.size = sz;
					break;

				case KDBGER_REQ_IO_WRITE:

					// Write IO content
					ptr = (s8 *)&pKdbgerCommPkt->kdbgerReqIoWritePkt.ioContent;
					ioAddr = pKdbgerCommPkt->kdbgerReqIoReadPkt.address;
					sz = pKdbgerCommPkt->kdbgerReqIoReadPkt.size;

					CbMemSet( pktBuf, 0, KDBGER_MAXSZ_PKT );
					for( i = 0 ; i < sz ; i++ ) {

						// Write IO data
						IoOutByte( *(ptr + i), ioAddr + i );
					}

					// Prepare the response packet
					pKdbgerCommPkt->kdbgerCommHdr.opCode = KDBGER_RSP_IO_WRITE;
					pKdbgerCommPkt->kdbgerCommHdr.pktLen = sizeof( kdbgerRspIoWritePkt_t );
					pKdbgerCommPkt->kdbgerCommHdr.errorCode = KDBGER_SUCCESS;
					pKdbgerCommPkt->kdbgerRspIoWritePkt.address = ioAddr;
					pKdbgerCommPkt->kdbgerRspIoWritePkt.size = sz;
					break;

				default:

					DbgPrint( "Unsupport OpCode = 0x%4.4X\n", pKdbgerCommPkt->kdbgerCommHdr.opCode );
					// Discard this packet
					kdbgerSetState( KDBGER_READY );
					goto done;
			}


			// Transit state to TRAN
			kdbgerSetState( KDBGER_PKT_TRAN );

			// Start to transmit
			for( idxBuf = 0 ; idxBuf < pKdbgerCommPkt->kdbgerCommHdr.pktLen ; idxBuf++ ) {

				IoOutByte( pktBuf[ idxBuf ], kdbgerPortAddr + UART_THR );
			}

			// Transit state to READY
			kdbgerSetState( KDBGER_READY );
		}
	}

done:

	// Reenable interrupt
	IoOutByte( UART_IER_RXRD, kdbgerPortAddr + UART_IER );
}


void kdbgerInitialization( kdbgerDebugPort_t port ) {

	// Indicate INIT state
	kdbgerSetState( KDBGER_INIT );

	// Setup UART base address
	kdbgerPortAddr = port;

    // Turn off Interrupt
    IoOutByte( 0x00, kdbgerPortAddr + UART_IER );

    // DLAB ON
    IoOutByte( UART_DLAB, kdbgerPortAddr + UART_LCR );

    // Baudrate = 115200
    // Divisor Low Byte
    IoOutByte( UART_BAUD_115200, kdbgerPortAddr + UART_DLL );

    // Divisor High Byte
    IoOutByte( 0x00, kdbgerPortAddr + UART_DLH );

    // DLAB = OFF, 8 Bits, No Parity, 1 Stop Bit
    IoOutByte( UART_STOPBIT_1 | UART_DATABIT_8, kdbgerPortAddr + UART_LCR );

	// Transit to READY state
	kdbgerSetState( KDBGER_READY );

	// Disable interrupt
	IntDisable();

	// Setup KDBGER interrupt handler
	IntRegInterrupt( IRQ_SERIAL0, IRQHandler( 4 ), kdbgerIntHandler );

	// Enable interrupt
	IntEnable();

	// Turn on interrupt
    IoOutByte( UART_IER_RXRD, kdbgerPortAddr + UART_IER );
}


