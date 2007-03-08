/*
 * Copyright (C) 2006 - 2007 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * @OLUXORG_LICENSE_HEADER_START@
 * @OLUXORG_LICENSE_HEADER_END@
 *
 */


#define     IDE_PORT_ADDR       0x01f0


#define     IDE_DATA            IDE_PORT_ADDR + 0x00
#define     IDE_FEATURE         IDE_PORT_ADDR + 0x01
#define     IDE_NSECTOR         IDE_PORT_ADDR + 0x02
#define     IDE_SECTOR          IDE_PORT_ADDR + 0x03
#define     IDE_LOWCYL          IDE_PORT_ADDR + 0x04
#define     IDE_HIGHCYL         IDE_PORT_ADDR + 0x05
#define     IDE_SELECT          IDE_PORT_ADDR + 0x06
#define     IDE_COMMAND         IDE_PORT_ADDR + 0x07
#define     IDE_DEVCTRL         IDE_PORT_ADDR + 0x0e

#define     IDE_ERROR           IDE_PORT_ADDR + 0x01
#define     IDE_STATUS          IDE_PORT_ADDR + 0x07
#define     IDE_ALTSTATUS       IDE_PORT_ADDR + 0x0e
#define     IDE_DRVADDR         IDE_PORT_ADDR + 0x0f


void IDEInit( void );


