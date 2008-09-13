/*
 * Copyright (C) 2006 - 2008 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * File: page.h
 * Description:
 * 	None
 *
 */



//
// Defintions
//
#define		NR_PDE			1
#define		NR_PTE			768


#define		PAGE_SIZE		0x1000
#define     PDE_ADDR    	0x00200000
#define		PTE_ADDR		(PDE_ADDR + PAGE_SIZE)


// Bit 0, Present (P) flag
#define		P_NPRESENT		0x000
#define		P_PRESENT		0x001


// Bit 1, Read/write (R/W) flag
#define		P_RDONLY		0x000
#define		P_RDWR			0x002


// Bit 2, User/supervisor (U/S) flag
#define		P_SUPERVISOR	0x000
#define		P_USER			0x004


// Bit 3, Page-level write-through (PWT) flag
#define		P_WBACK			0x000
#define		P_WTHROUGH		0x008


// Bit 4, Page-level cache disable (PCD) flag
#define		P_CACHE_EN		0x000
#define		P_CACHE_DIS		0x010


// Bit 5, Accessed (A) flag
#define		P_NOACCESS		0x000
#define		P_ACCESSED		0x020


// Bit 6, Dirty (D) flag
#define		P_NODIRTY		0x000
#define		P_DIRTY			0x040


// Bit 7, Page size (PS) flag
#define		P_PSIZE_4KB		0x000
#define		P_PSIZE_4MB		0x080


// Bit 8, Global (G) flag
#define		P_GLO_NORMAL	0x000
#define		P_GLO_NOFLUSH	0x100


// Convenient definitions of page
#define		P_SUP_WT_RW_4K	(P_PRESENT | P_RDWR | P_SUPERVISOR | P_WTHROUGH | P_CACHE_DIS | P_NOACCESS | P_NODIRTY | P_PSIZE_4KB | P_GLO_NORMAL)


#define		P_SUP_WT_RW_4M	(P_PRESENT | P_RDWR | P_SUPERVISOR | P_WTHROUGH | P_CACHE_DIS | P_NOACCESS | P_NODIRTY | P_PSIZE_4MB | P_GLO_NORMAL)


// Page Fault Error Code
#define		PF_PRESENT		0x01
#define		PF_RW			0x02
#define		PF_MODE			0x04
#define		PF_RSVD			0x08


//
// Structures
//
typedef enum {

	ADDRESS_RANGE_UNDEFINED,
	ADDRESS_RANGE_MEMORY,
	ADDRESS_RANGE_RESERVED,
	ADDRESS_RANGE_ACPI,
	ADDRESS_RANGE_NVS,
	ADDRESS_RANGE_UNUSUABLE,
} E820Type;


PACKED_STRUCT _E820Result {

    u32         BaseAddrLow;
    u32         BaseAddrHigh;
    u32         LengthLow;
    u32         LengthHigh;
    E820Type	RecType;
    u32         Attributes;

} E820Result;



//
// Prototypes
//
void MmPageInit( void );
s8 *MmShowE820Type( E820Type RecType );
void MmEnablePaging( volatile u32 *Ptr );
void MmDisablePaging( void );
void MmEnablePSE( void );
void MmDisablePSE( void );



