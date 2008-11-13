/*
 * Copyright (C) 2006 - 2008 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * File: interrupt.h
 * Description:
 * 	None
 *
 */


//
// Definitions
//
#define     NR_VECTOR				256
#define     SZ_INT_ENTRY			8
#define		SZ_INT_TABLE			(NR_VECTOR * SZ_INT_ENTRY)
#define		SZ_INT_STACK			4096

#define     HW_INT_START			0x20
#define		NR_HW_IRQ				(NR_VECTOR - HW_INT_START)


// IDT Gate Type
#define		IDT_F_TASK				0x05
#define		IDT_F_INT				0x06
#define		IDT_F_TRAP				0x07

// Size of gate
#define		IDT_F_SZ_16				0x00
#define		IDT_F_SZ_32				0x08

// Privilege level
#define		IDT_F_DPL_0				0x00
#define		IDT_F_DPL_1				0x20
#define		IDT_F_DPL_2				0x40
#define		IDT_F_DPL_3				0x60

// Segment present flag
#define		IDT_F_NPRESENT			0x00
#define		IDT_F_PRESENT			0x80

#define     GATE_INT_FLAG   		(IDT_F_INT | IDT_F_SZ_32 | IDT_F_DPL_0 | IDT_F_PRESENT)
#define     GATE_TRAP_FLAG  		(IDT_F_TRAP | IDT_F_SZ_32 | IDT_F_DPL_0 | IDT_F_PRESENT)


#define     IRQHandler( IRQNUM )		IrqHandler_##IRQNUM
#define		ExternIRQHandler( IRQNUM )	extern void IrqHandler_##IRQNUM( void )


// Hardware IRQ Numbers
#define		IRQ_TIMER				0x00
#define		IRQ_KEYBOARD			0x01
#define		IRQ_SERIAL0				0x04



#ifndef __ASM__
//
// Structures
//
typedef struct PACKED _IDTEntry {

    u16   OffsetLSW;
    u16   SegSelect;
    u8    Reserved;
    u8    Flags;
    u16   OffsetMSW;

} IDTEntry;


typedef struct PACKED _IDTPtr {

    u16   Limit;
    u32   BaseAddr;

} IDTPtr;


typedef struct PACKED _IntHandlerLst {

	void (*IrqHandler)( u8 IrqNum );

} IntHandlerLst;



//
// Prototypes
//
void IntInitInterrupt( void );

void IntLoadIDTRegister( IDTPtr *Ptr );
void IntSetIDT( u32 Index, void *Handler, void *SegSel, u8 Flags );
void IntDelIDT( u32 Index );

void IntDisable( void );
void IntEnable( void );

void IntRegInterrupt( u32 IntNum, void *IrqHandler, void (*HwIntHandler)( u8 IrqNum ) );
void IntUnregInterrupt( u32 IntNum );

void IntHandleIRQ( u32 IrqNum, GeneralRegisters *Regs );
void IntIssueEOI( void );

void IntShowIDTTable( void );
#endif



