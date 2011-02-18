/*
 * Copyright (C) 2006 - 2007 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * @OLUXORG_LICENSE_HEADER_START@
 * @OLUXORG_LICENSE_HEADER_END@
 *
 * pci.c -- OluxOS IA32 Pci routines
 *
 */
#include <types.h>
#include <ia32/io.h>
#include <ia32/debug.h>
#include <driver/pci.h>
#include <driver/console.h>


//
// PciReadConfigByte
//
// Input:
//  address : Pci device address
//  offset  : Offset address of register
//
// Return:
//  Register value in byte
//
// Description:
//  Read Pci configuration space register in byte
//
u8 PciReadConfigByte( u32 address, u8 offset ) {

    IoOutDWord( address | offset , PCI_PORT_ADDR );
    return IoInByte( PCI_PORT_DATA );
}


//
// PciWriteConfigByte
//
// Input:
//  address : Pci device address
//  offset  : Offset address of register
//  value   : Value to write
//
// Return:
//  Register value in byte
//
// Description:
//  Write byte to Pci configuration space register
//
void PciWriteConfigByte( u32 address, u8 offset, u8 value ) {

    IoOutDWord( address | offset, PCI_PORT_ADDR );
    IoOutByte( value, PCI_PORT_ADDR );
}


//
// PciReadConfigWord
//
// Input:
//  address : Pci device address
//  offset  : Offset address of register
//
// Return:
//  Register value in word
//
// Description:
//  Read Pci configuration space register in word
//
u16 PciReadConfigWord( u32 address, u8 offset ) {

    IoOutDWord( address | offset , PCI_PORT_ADDR );
    return IoInWord( PCI_PORT_DATA );
}


//
// PciWriteConfigWord
//
// Input:
//  address : Pci device address
//  offset  : Offset address of register
//  value   : Value to write
//
// Return:
//  Register value in word
//
// Description:
//  Write word to Pci configuration space register
//
void PciWriteConfigWord( u32 address, u8 offset, u16 value ) {

    IoOutDWord( address | offset, PCI_PORT_ADDR );
    IoOutWord( value, PCI_PORT_ADDR );
}


//
// PciReadConfigDWord
//
// Input:
//  address : Pci device address
//  offset  : Offset address of register
//
// Return:
//  Register value in double word
//
// Description:
//  Read Pci configuration space register in double word
//
u32 PciReadConfigDWord( u32 address, u8 offset ) {

    IoOutDWord( address | offset , PCI_PORT_ADDR );
    return IoInDWord( PCI_PORT_DATA );
}


//
// PciWriteConfigDWord
//
// Input:
//  address : Pci device address
//  offset  : Offset address of register
//  value   : Value to write
//
// Return:
//  Register value in double word
//
// Description:
//  Write double word to Pci configuration space register
//
void PciWriteConfigDWord( u32 address, u8 offset, u32 value ) {

    IoOutDWord( address | offset, PCI_PORT_ADDR );
    IoOutDWord( value, PCI_PORT_ADDR );
}


//
// PciCalBaseAddr
//
// Input:
//  bus     : Pci bus number
//  dev     : Pci device number
//  func    : Pci function number
//
// Return:
//  Pci base address
//
// Description:
//  Calculate Pci base address by bus, device, and function numbers
//
u32 PciCalBaseAddr( u16 bus, u8 dev, u8 func ) {

    return PCI_ENABLE_BIT
		| (((u32)bus) << 16)
		| ((((u32)dev) & 0x1F) << 11)
		| ((((u32)func) & 0x07) << 8);
}


//
// PciDetectDevice
//
// Input:
//  None
//
// Return:
//  None
//
// Description:
//  Scaning all Pci devices on the bus
//
void PciDetectDevice( void ) {

    u32 value;
    u16 bus;
    u8 dev, func;

    for( bus = 0 ; bus <= PCI_BUS_MAX ; bus++ )
        for( dev = 0 ; dev <= PCI_DEV_MAX ; dev++ )
            for( func = 0 ; func <= PCI_FUN_MAX ; func++ ) {
            
                value = PciReadConfigDWord( PciCalBaseAddr( bus, dev, func ), 0 );
                if( value != 0xffffffff ) {
                
                    DbgPrint( "Pci Bus : %4X, Dev : %4X, Func : %4X, Vid = %4X, Did = %4X\n"
                                    , bus, dev, func, (value & 0xffff), ((value >> 16) & 0xffff) );
                }
            }
}


