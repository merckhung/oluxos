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


static u16 kdbgerPortAddr;
static u8 kdbgerState = KDBGER_UNKNOWN;


static void kdbgerSetState( kdbgerState_t state ) {

	kdbgerState = state;
}


static u8 kdbgerGetState( void ) {

	return kdbgerState;
}


static void kdbgerIntHandler( u8 IrqNum ) {

	u8 iirState;
	u8 c;

	// Read IIR
	iirState = IoInByte( kdbgerPortAddr + UART_IIR );
	if( iirState & UART_IIR_PND ) {

		// No interrupt pending
		DbgPrint( "No interrupt pending\n" );
		return;
	}

	// Preliminary handle IIR
	iirState = (iirState & UART_IIR_ID_MASK) >> UART_IIR_ID_OFFET;
	switch( iirState ) {

		case UART_IIR_TBE:
			//DbgPrint( "UART_IIR_TBE\n" );
			break;

		case UART_IIR_DR:
			//DbgPrint( "UART_IIR_DR\n" );
			c = IoInByte( kdbgerPortAddr + UART_RBR );
            DbgPrint( "%c", c );
            IoOutByte( c, kdbgerPortAddr + UART_THR );
			break;

		case UART_IIR_SEB:
			DbgPrint( "UART_IIR_SEB\n" );
			break;

		default:
		case UART_IIR_CHG_INPUT:
			DbgPrint( "UART_IIR_CHG_INPUT\n" );
			break;
	}
}


void kdbgerInitialization( kdbgerDebugPort_t port ) {

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
    IoOutByte( (UART_STOPBIT_1 | UART_DATABIT_8), kdbgerPortAddr + UART_LCR );

    // Turn off flow control
    //IoOutByte( 0x00, kdbgerPortAddr + UART_MCR );

	kdbgerSetState( KDBGER_READY );

	// Disable interrupt
	IntDisable();

	// Setup KDBGER interrupt handler
	IntRegInterrupt( IRQ_SERIAL0, IRQHandler( 4 ), kdbgerIntHandler );

	// Enable interrupt
	IntEnable();

	// Turn on interrupt
    IoOutByte( UART_IER_RXRD | UART_IER_TBE, kdbgerPortAddr + UART_IER );
}


