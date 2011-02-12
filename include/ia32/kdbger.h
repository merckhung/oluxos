/*
 * Copyright (C) 2006 - 2011 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * File: kdbger.h
 * Description:
 *  OluxOS Kernel Debugger header file
 *
 */


// 16550A UART registers
#define UART_REG_RBR		0x00	// RO
#define UART_REG_THR		0x00	// WO
#define UART_REG_IER		0x01	// RW
#define UART_REG_IIR		0x02	// RO
#define UART_REG_FCR		0x02	// WO
#define UART_REG_LCR		0x03	// RW
#define UART_REG_MCR		0x04	// RW
#define UART_REG_LSR		0x05	// RW
#define UART_REG_MSR		0x06	// RW
#define UART_REG_SCR		0x07	// RW
#define UART_REG_DLL		0x00	// RW
#define UART_REG_DLH		0x01	// RW

// IER
#define UART_IER_RXRD		0x01
#define UART_IER_TBE        0x02
#define UART_IER_ERBK       0x04
#define UART_IER_SINP		0x08

// IIR
#define UART_IIR_PND		0x01
#define UART_IIR_ID_MASK	0x06
#define UART_IIR_ID_OFFET	1
#define UART_IIR_CHG_INPUT	0x00
#define UART_IIR_TBE		0x01
#define UART_IIR_DR			0x02
#define UART_IIR_SEB		0x03

// FCR
#define UART_FCR_FE			0x01
#define UART_FCR_RFR		0x02
#define UART_FCR_XFR		0x04
#define UART_FCR_DMS		0x08
#define UART_FCR_TL_1B		0x00
#define UART_FCR_TL_4B		0x40
#define UART_FCR_TL_8B		0x80
#define UART_FCR_TL_14B		0xC0

// LCR
#define UART_LCR_DLAB		0x80
#define UART_LCR_BRK		0x40

#define UART_PARITY_NONE	0x00
#define UART_PARITY_ODD		0x08
#define UART_PARITY_EVEN	0x18
#define UART_PARITY_MARK	0x28
#define UART_PARITY_SPACE	0x38

#define UART_STOPBIT_1		0x00
#define UART_STOPBIT_2		0x04

#define UART_DATABIT_8		0x03
#define UART_DATABIT_7		0x02
#define UART_DATABIT_6		0x01
#define UART_DATABIT_5		0x00

// MCR
#define UART_DTR			0x01
#define UART_RTS			0x02
#define UART_OUT1			0x04
#define UART_OUT2			0x08
#define UART_LOOPBACK		0x10

// LSR
#define UART_LSR_RXDR		0x01
#define UART_LSR_OVER		0x02
#define UART_LSR_PARE		0x04
#define UART_LSR_FRME		0x08
#define UART_LSR_BREK		0x10
#define UART_LSR_TBE		0x20
#define UART_LSR_TXE		0x40
#define UART_LSR_FIFOE		0x80

// MSR
#define UART_MSR_DCTS		0x01
#define UART_MSR_DDSR		0x02
#define UART_MSR_TERI		0x04
#define UART_MSR_DDCD		0x08
#define UART_MSR_CTS		0x10
#define UART_MSR_DSR		0x20
#define UART_MSR_RI			0x40
#define UART_MSR_DCD		0x80

// Baud Rate
#define UART_BAUD_115200	0x01
#define UART_BAUD_38400		0x03
#define	UART_BAUD_19200		0x06
#define UART_BAUD_9600		0x0C
#define UART_BAUD_2400		0x30
#define UART_BAUD_1200		0x60


#define KDBGER_MAXSZ_PKT	4096


typedef enum {

	UART_PORT0 = 0x03F8,
	UART_PORT1 = 0x02F8,

} kdbgerDebugPort_t;


typedef enum {

	KDBGER_UNKNOWN = 0,
	KDBGER_INIT,
	KDBGER_READY,
	KDBGER_PKT_TRAN,
	KDBGER_PKT_RECV,
	KDBGER_PKT_DONE,

} kdbgerState_t;


typedef enum {

	KDBGER_BAUD_115200 = 0x01,
	KDBGER_BAUD_38400 = 0x03,
	KDBGER_BAUD_19200 = 0x06,
	KDBGER_BAUD_9600 = 0x0C,
	KDBGER_BAUD_2400 = 0x30,
	KDBGER_BAUD_1200 = 0x60,

} kdbgerBaudrate_t;


typedef enum _kdbgerReqOpCode {

	KDBGER_REQ_CONNECT = 1,

	KDBGER_REQ_MEM_READ,
	KDBGER_RSP_MEM_READ,

	KDBGER_REQ_MEM_WRITE,
	KDBGER_RSP_MEM_WRITE,

	KDBGER_REQ_IO_READ,
	KDBGER_RSP_IO_READ,

	KDBGER_REQ_IO_WRITE,
    KDBGER_RSP_IO_WRITE,

	KDBGER_REQ_PCI_READ,
	KDBGER_RSP_PCI_READ,

	KDBGER_REQ_PCI_WRITE,
	KDBGER_RSP_PCI_WRITE,

	KDBGER_RSP_CPU_EXCEPTION,

} kdbgerOpCode_t;


typedef enum _kdbgErrorCode {

	KDBGER_SUCCESS = 0,
	KDBGER_FAILURE,

} kdbgErrorCode_t;


// Common packet
typedef struct PACKED _kdbgerCommHdr {

	u16		opCode;

	union {

		u16		pad;
		u16		errorCode;
	};

	u32		pktLen;

} kdbgerCommHdr_t;


// Memory space Read/Write packets
typedef struct PACKED {

	kdbgerCommHdr_t			kdbgerCommHdr;
	u64						address;
	u32						size;

} kdbgerReqMemReadPkt_t, kdbgerRspMemWritePkt_t;


typedef struct PACKED {

	kdbgerCommHdr_t			kdbgerCommHdr;
	u64						address;
	u32						size;
	s8						*memContent;

} kdbgerRspMemReadPkt_t, kdbgerReqMemWritePkt_t;


// IO space Read/Write packets
typedef struct PACKED {

	kdbgerCommHdr_t			kdbgerCommHdr;
	u16						address;
	u32						size;

} kdbgerReqIoReadPkt_t, kdbgerRspIoWritePkt_t;


typedef struct PACKED {

	kdbgerCommHdr_t			kdbgerCommHdr;
	u16						address;
	u32						size;
	s8						*ioContent;

} kdbgerRspIoReadPkt_t, kdbgerReqIoWritePkt_t;


// PCI config Read/Write packets
typedef struct PACKED {

	kdbgerCommHdr_t			kdbgerCommHdr;
	u32						address;
	u16						size;

} kdbgerReqPciReadPkt_t, kdbgerRspPciWritePkt_t;


typedef struct PACKED {

	kdbgerCommHdr_t			kdbgerCommHdr;
	u32						address;
	u16						size;
	s8						*pciContent;

} kdbgerRspPciReadPkt_t, kdbgerReqPciWritePkt_t;


typedef struct PACKED {

	kdbgerCommHdr_t			kdbgerCommHdr_t;
	u32						exNum;

} kdbgerRspCpuExceptionPkt_t;


typedef struct PACKED {

	union {

		// Common
		kdbgerCommHdr_t				kdbgerCommHdr;

		// Memory Read
		kdbgerReqMemReadPkt_t		kdbgerReqMemReadPkt;
		kdbgerRspMemReadPkt_t		kdbgerRspMemReadPkt;

		// Memory Write
		kdbgerReqMemWritePkt_t		kdbgerReqMemWritePkt;
		kdbgerRspMemWritePkt_t		kdbgerRspMemWritePkt;

		// IO Read
		kdbgerReqIoReadPkt_t		kdbgerReqIoReadPkt;
		kdbgerRspIoReadPkt_t		kdbgerRspIoReadPkt;

		// IO Write
		kdbgerReqIoWritePkt_t		kdbgerReqIoWritePkt;
		kdbgerRspIoWritePkt_t		kdbgerRspIoWritePkt;

		// PCI Read
		kdbgerReqPciReadPkt_t		kdbgerReqPciReadPkt;
		kdbgerRspPciReadPkt_t		kdbgerRspPciReadPkt;

		// PCI Write
		kdbgerReqPciWritePkt_t		kdbgerReqPciWritePkt;
		kdbgerRspPciWritePkt_t		kdbgerRspPciWritePkt;

		// CPU Exception
		kdbgerRspCpuExceptionPkt_t	kdbgerRspCpuExceptionPkt;
	};

} kdbgerCommPkt_t;


// Prototypes
void kdbgerInitialization( kdbgerDebugPort_t port );


