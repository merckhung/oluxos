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
__u8 PciReadConfigByte( __u32 address, __u8 offset ) {

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
void PciWriteConfigByte( __u32 address, __u8 offset, __u8 value ) {

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
__u16 PciReadConfigWord( __u32 address, __u8 offset ) {

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
void PciWriteConfigWord( __u32 address, __u8 offset, __u16 value ) {

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
__u32 PciReadConfigDWord( __u32 address, __u8 offset ) {

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
void PciWriteConfigDWord( __u32 address, __u8 offset, __u32 value ) {

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
__u32 PciCalBaseAddr( __u8 bus, __u8 dev, __u8 func ) {

    return PCI_ENABLE_BIT | (bus << 16) | ((dev & 0x1f) << 11) | ((func & 0x07) << 8);
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

    __u32 value;
    __u16 bus;
    __u8 dev, func;


    for( bus = 0 ; bus <= PCI_BUS_MAX ; bus++ ) {
    
        for( dev = 0 ; dev <= PCI_DEV_MAX ; dev++ ) {
        
            for( func = 0 ; func <= PCI_FUN_MAX ; func++ ) {
            
                value = PciReadConfigDWord( PciCalBaseAddr( bus, dev, func ), 0 );
                if( value != 0xffffffff ) {
                
                    DbgPrint( "Pci Bus : %4X, Dev : %4X, Func : %4X, Vid = %4X, Did = %4X\n"
                                    , bus, dev, func, (value & 0xffff), ((value >> 16) & 0xffff) );
                }
            }
        }
    }
}


