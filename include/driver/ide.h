/*
 * Copyright (C) 2006 - 2008 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * File: ide.h
 * Description:
 *  OluxOS IDE driver header file
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

#define	IDE_SZ_SECTOR			512


void IDEReadData( s8 *buf );
void IDEReadSector( u32 sector, s8 *buf );
void IDEWriteSector( u32 sector, s8 *buf );
void IDEGetIdentify( void );
void IDEInit( void );


