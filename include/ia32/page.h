/*
 * Copyright (C) 2006 - 2007 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * @OLUXORG_LICENSE_HEADER_START@
 * @OLUXORG_LICENSE_HEADER_END@
 *
 */


#define     MEM_SIZE    32 * 1024 * 1024
#define     PTENUM      8 * 1024
#define     PDENUM      8
#define     PAGEADDR    0x200000


void MmPageInit( void );


enum {

	ADDRESS_RANGE_UNDEFINED,
	ADDRESS_RANGE_MEMORY,
	ADDRESS_RANGE_RESERVED,
	ADDRESS_RANGE_ACPI,
	ADDRESS_RANGE_NVS,
	ADDRESS_RANGE_UNUSUABLE,
};


typedef struct __attribute__((packed)) _E820Result {

    u32         BaseAddrLow;
    u32         BaseAddrHigh;
    u32         LengthLow;
    u32         LengthHigh;
    u32         RecType;
    u32         Attributes;

} E820Result;


