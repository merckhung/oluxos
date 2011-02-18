/*
 * Copyright (C) 2006 - 2008 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * File: pci.h
 * Description:
 *  OluxOS PCI driver header file
 *
 */


#define     PCI_ENABLE_BIT      0x80000000
#define     PCI_BUS_MAX         0x00FF
#define     PCI_DEV_MAX         0x1F
#define     PCI_FUN_MAX         0x07

#define     PCI_PORT_ADDR       0x0CF8
#define     PCI_PORT_DATA       0x0CFC


u8 PciReadConfigByte( u32 address, u8 offset );
void PciWriteConfigByte( u32 address, u8 offset, u8 value );
u16 PciReadConfigWord( u32 address, u8 offset );
void PciWriteConfigWord( u32 address, u8 offset, u16 value );
u32 PciReadConfigDWord( u32 address, u8 offset );
void PciWriteConfigDWord( u32 address, u8 offset, u32 value );
u32 PciCalBaseAddr( u16 bus, u8 dev, u8 func );

void PciDetectDevice( void );


