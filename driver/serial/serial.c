/*
 * Copyright (C) 2006 -  2008 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * File: serial.c
 * Description:
 * 	OluxOS IA32 Serial Port Driver Routines
 *
 */
#include <types.h>
#include <ia32/platform.h>
#include <ia32/interrupt.h>
#include <ia32/io.h>
#include <ia32/debug.h>
#include <driver/serial.h>



ExternIRQHandler( 4 );



void SrInit( void ) {


    // Turn off Interrupt
    IoOutByte( 0x00, UART_DEF_IER );


    // DLAB ON
    IoOutByte( UART_DLAB, UART_DEF_LCR );


    // Baudrate = 115200
    // Divisor Low Byte
    IoOutByte( UART_BAUD_115200, UART_DEF_DLL );


    // Divisor High Byte
    IoOutByte( 0x00, UART_DEF_DLH );


    // DLAB = OFF, 8 Bits, No Parity, 1 Stop Bit
    IoOutByte( (UART_WLS8 | UART_STB_1), UART_DEF_LCR );


	// FIFO off
	IoOutByte( 0x00, UART_DEF_FCR );


    // FIFO control
	//IoOutByte( (UART_ITL14 | UART_RESETTF | UART_RESETRF | UART_TRFIFOE), UART_DEF_FCR );


    // Turn off flow control
    IoOutByte( 0x00, UART_DEF_MCR );
}



void SrInitInterrupt( void ) {


	// Register interrupt handler
	IntRegInterrupt( IRQ_SERIAL0, IRQHandler( 4 ), SrIntHandler );


	// Enable interrupt
	IntDisable();


	// Enable UART interrupt
	IoOutByte( (UART_RAVIE | UART_TIE | UART_RLSE | UART_MIE), UART_DEF_IER );


	// Disable interrupt
	IntEnable();
}



void SrIntHandler( u8 IrqNum ) {

	u8 tmp;


	// Identify interrupt
	tmp = IoInByte( UART_DEF_IIR );
	//DbgPrint( "IIR = 0x%2.2X ", tmp );


	tmp = IoInByte( UART_DEF_LCR );
	//DbgPrint( "LCR = 0x%2.2X ", tmp );


	tmp = IoInByte( UART_DEF_MSR );
	//DbgPrint( "MSR = 0x%2.2X ", tmp );


	tmp = IoInByte( UART_DEF_LSR );
	//DbgPrint( "LSR = 0x%2.2X\n", tmp );


	if( tmp & UART_DR ) {
	
		tmp = IoInByte( UART_DEF_RBR );
		TcPrint( "%c", tmp );
	}
}



void SrPutChar( s8 c ) {

    IoOutByte( c, UART_DEF_THR ); 
}



s8 SrGetChar( void ) {

    if( IoInByte( UART_DEF_LSR ) & UART_DR ) {
    
        return (IoInByte( UART_DEF_RBR ) & 0xFF);   
    }

    return 0;
}



