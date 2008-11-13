/*
 * Copyright (C) 2006 - 2008 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * File: platform.h
 * Description:
 * 	None
 *
 */



//
// Definitions
//
#define KRN_LOAD_ADDR   0x00100000
#define KRN_BOOT_ADDR   0x7c00
#define KRN_BOOT_SEG    0x07c0
#define KRN_BLOCK_SZ    0x200
#define KRN_STACK_SZ    4096


#define E820_COUNT      0x9000
#define E820_BASE       0x9004



#ifndef __ASM__
//
// Structures
//
typedef struct PACKED _GeneralRegisters {

	u32		eax;		// 0
	u32		ecx;		// 4
	u32		edx;		// 8
	u32		ebx;		// 12

	u32		esp;		// 16
	u32		ebp;		// 20
	u32		esi;		// 24
	u32		edi;		// 28

	u32		eip;		// 32
	u32		eflags;		// 36

	u16		cs;			// 40
	u16		rvsd0;
	u16		ss;			// 44
	u16		rvsd1;
	u16		ds;			// 48
	u16		rvsd2;
	u16		es;			// 52
	u16		rvsd3;
	u16		fs;			// 56
	u16		rvsd4;
	u16		gs;			// 60
	u16		rvsd5;

} GeneralRegisters;



//
// Prototypes
//
#endif



