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
static s32 checker = 0;


static void kdbgerSetState( kdbgerState_t state ) {

	kdbgerState = state;
}


static u8 kdbgerGetState( void ) {

	return kdbgerState;
}


static void kdbgerIntrEnable( void ) {

	IoOutByte( UART_IER_RXRD, kdbgerPortAddr + UART_REG_IER );
}


static void kdbgerIntrDisable( void ) {

	IoOutByte( 0x00, kdbgerPortAddr + UART_REG_IER );
}


static void kdbgerFifoEnable( void ) {

	// FIFO Enable, TX & RX, 14 bytes
	IoOutByte( (UART_FCR_FE | UART_FCR_RFR | UART_FCR_XFR | UART_FCR_TL_14B), kdbgerPortAddr + UART_REG_FCR );
}


static void kdbgerIntHandler( u8 IrqNum ) {

	u8 lsrState;
	kdbgerCommPkt_t *pKdbgerCommPkt = (kdbgerCommPkt_t *)pktBuf;
	s8 *ptr;
	volatile u8 *phyMem;
	u32 i, sz;
	u64 addr;
	u16 ioAddr;
	u32 pciAddr;
	u16 pciSz;
	s8 restBuf[ KDBGER_FIFO_SZ ];
	s32 restLen;

    // Disable interrupt
    kdbgerIntrDisable();

	// Make sure the state != UNKNOWN
	if( kdbgerGetState() == KDBGER_UNKNOWN ) {

		DbgPrint( "Kernel Debugger internal state is unknown\n" );
		goto done;
	}

	// Read LSR
	lsrState = IoInByte( kdbgerPortAddr + UART_REG_LSR );
	if( lsrState & UART_LSR_RXDR ) {

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

		// Receive data of a request packet from FIFO
		for( ; ; ) {

			// Read a byte
			pktBuf[ idxBuf++ ] = IoInByte( kdbgerPortAddr + UART_REG_RBR );
			if( !(IoInByte( kdbgerPortAddr + UART_REG_LSR ) & UART_LSR_RXDR) )
				break;
		}

		// Response to a request
		if( (kdbgerGetState() == KDBGER_PKT_RECV) && (idxBuf >= sizeof( kdbgerCommHdr_t ))
			&& (idxBuf >= pKdbgerCommPkt->kdbgerCommHdr.pktLen) ) {

			if( idxBuf != pKdbgerCommPkt->kdbgerCommHdr.pktLen ) {

				restLen = idxBuf - pKdbgerCommPkt->kdbgerCommHdr.pktLen;
				if( restLen <= KDBGER_FIFO_SZ )
					CbMemCpy( restBuf, pktBuf + pKdbgerCommPkt->kdbgerCommHdr.pktLen, restLen );
				else {
				
					// Internal error
					kdbgerSetState( KDBGER_UNKNOWN );	
					goto done;
				}
			}
			else
				restLen = 0;

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

                case KDBGER_REQ_PCI_READ:

                    // Read PCI config space
                    ptr = (s8 *)&pKdbgerCommPkt->kdbgerRspPciReadPkt.pciContent;
                    pciAddr = pKdbgerCommPkt->kdbgerReqPciReadPkt.address;
                    pciSz = pKdbgerCommPkt->kdbgerReqPciReadPkt.size;

                    CbMemSet( pktBuf, 0, KDBGER_MAXSZ_PKT );
                    for( i = 0 ; i < pciSz ; i++ ) {

                        // Read PCI config data
                        //*(ptr + i) = IoInByte( ioAddr + i );
                    }

                    // Prepare the response packet
                    pKdbgerCommPkt->kdbgerCommHdr.opCode = KDBGER_RSP_PCI_READ;
                    pKdbgerCommPkt->kdbgerCommHdr.pktLen =
                        sizeof( kdbgerRspPciReadPkt_t ) - sizeof( s8 * ) + pciSz;
                    pKdbgerCommPkt->kdbgerCommHdr.errorCode = KDBGER_SUCCESS;
                    pKdbgerCommPkt->kdbgerRspIoReadPkt.address = pciAddr;
                    pKdbgerCommPkt->kdbgerRspIoReadPkt.size = pciSz;
                    break;

				case KDBGER_REQ_PCI_WRITE:

					// Write PCI config space
					ptr = (s8 *)&pKdbgerCommPkt->kdbgerReqPciWritePkt.pciContent;
					pciAddr = pKdbgerCommPkt->kdbgerReqPciReadPkt.address;
					pciSz = pKdbgerCommPkt->kdbgerReqPciReadPkt.size;

					CbMemSet( pktBuf, 0, KDBGER_MAXSZ_PKT );
					for( i = 0 ; i < pciSz ; i++ ) {

						// Write PCI config data
						//IoOutByte( *(ptr + i), ioAddr + i );
					}

					// Prepare the response packet
					pKdbgerCommPkt->kdbgerCommHdr.opCode = KDBGER_RSP_PCI_WRITE;
					pKdbgerCommPkt->kdbgerCommHdr.pktLen = sizeof( kdbgerRspPciWritePkt_t );
					pKdbgerCommPkt->kdbgerCommHdr.errorCode = KDBGER_SUCCESS;
					pKdbgerCommPkt->kdbgerRspIoWritePkt.address = pciAddr;
					pKdbgerCommPkt->kdbgerRspIoWritePkt.size = pciSz;
					break;

				default:

					DbgPrint( "Unsupport OpCode = 0x%4.4X\n", pKdbgerCommPkt->kdbgerCommHdr.opCode );
					DbgPrint( "Len = %d\n", pKdbgerCommPkt->kdbgerCommHdr.pktLen );
					DbgPrint( "idxBuf = %d\n", idxBuf );

					CbMemSet( pktBuf, 0, KDBGER_MAXSZ_PKT );
					pKdbgerCommPkt->kdbgerCommHdr.opCode = KDBGER_RSP_NACK;
					pKdbgerCommPkt->kdbgerCommHdr.pktLen = sizeof( kdbgerCommHdr_t );
					pKdbgerCommPkt->kdbgerCommHdr.errorCode = KDBGER_SUCCESS;
					break;
			}


			// Transit state to TRAN
			kdbgerSetState( KDBGER_PKT_TRAN );

			// Start to transmit
			for( idxBuf = 0 ; idxBuf < pKdbgerCommPkt->kdbgerCommHdr.pktLen ; )
				if( IoInByte( kdbgerPortAddr + UART_REG_LSR ) & UART_LSR_TXE )
					IoOutByte( pktBuf[ idxBuf++ ], kdbgerPortAddr + UART_REG_THR );

			if( restLen ) {

				CbMemCpy( pktBuf, restBuf, restLen );
				idxBuf = restLen;
				kdbgerSetState( KDBGER_PKT_RECV );
			}
			else
				kdbgerSetState( KDBGER_READY );
		}
	}

done:

	checker++;

	// Reenable interrupt
	kdbgerIntrEnable();
}


static void kdbgerSetupBaudrate( kdbgerBaudrate_t baud ) {

	s8 tmp;

	// DLAB On
	tmp = IoInByte( kdbgerPortAddr + UART_REG_LCR );
    IoOutByte( tmp | UART_LCR_DLAB, kdbgerPortAddr + UART_REG_LCR );

    // Divisor
    IoOutByte( baud, kdbgerPortAddr + UART_REG_DLL );
    IoOutByte( 0x00, kdbgerPortAddr + UART_REG_DLH );

    // DLAB Off
    IoOutByte( tmp, kdbgerPortAddr + UART_REG_LCR );
}


void kdbgerInitialization( kdbgerDebugPort_t port ) {

	// Indicate INIT state
	kdbgerSetState( KDBGER_INIT );

	// Setup UART base address
	kdbgerPortAddr = port;

    // Turn off Interrupt
    kdbgerIntrDisable();

	// Setup baudrate
	kdbgerSetupBaudrate( KDBGER_BAUD_115200 );

    // 8 Bits, No Parity, 1 Stop Bit
    IoOutByte( UART_STOPBIT_1 | UART_DATABIT_8, kdbgerPortAddr + UART_REG_LCR );

	// Enable FIFO
	kdbgerFifoEnable();

	// Transit to READY state
	kdbgerSetState( KDBGER_READY );

	// Disable interrupt
	IntDisable();

	// Setup interrupt handler
	IntRegInterrupt( IRQ_SERIAL0, IRQHandler( 4 ), kdbgerIntHandler );

	// Enable interrupt
	IntEnable();

	// Turn on interrupt
    kdbgerIntrEnable();
}


