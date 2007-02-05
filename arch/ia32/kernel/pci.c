/*
 * Copyright (C) 2006 - 2007 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * @OLUXORG_LICENSE_HEADER_START@
 * @OLUXORG_LICENSE_HEADER_END@
 *
 * pci.c -- OluxOS IA32 PCI routines
 *
 */
#include <ia32/types.h>
#include <ia32/io.h>
#include <ia32/pci.h>
#include <ia32/console.h>


//
// PCIReadConfigByte
//
// Input:
//  address : PCI device address
//  offset  : Offset address of register
//
// Return:
//  Register value in byte
//
// Description:
//  Read PCI configuration space register in byte
//
__u8 PCIReadConfigByte( __u32 address, __u8 offset ) {

    IoOutDWord( address | offset , PCI_PORT_ADDR );
    return IoInByte( PCI_PORT_DATA );
}


//
// PCIWriteConfigByte
//
// Input:
//  address : PCI device address
//  offset  : Offset address of register
//  value   : Value to write
//
// Return:
//  Register value in byte
//
// Description:
//  Write byte to PCI configuration space register
//
void PCIWriteConfigByte( __u32 address, __u8 offset, __u8 value ) {

    IoOutDWord( address | offset, PCI_PORT_ADDR );
    IoOutByte( value, PCI_PORT_ADDR );
}


//
// PCIReadConfigWord
//
// Input:
//  address : PCI device address
//  offset  : Offset address of register
//
// Return:
//  Register value in word
//
// Description:
//  Read PCI configuration space register in word
//
__u16 PCIReadConfigWord( __u32 address, __u8 offset ) {

    IoOutDWord( address | offset , PCI_PORT_ADDR );
    return IoInWord( PCI_PORT_DATA );
}


//
// PCIWriteConfigWord
//
// Input:
//  address : PCI device address
//  offset  : Offset address of register
//  value   : Value to write
//
// Return:
//  Register value in word
//
// Description:
//  Write word to PCI configuration space register
//
void PCIWriteConfigWord( __u32 address, __u8 offset, __u16 value ) {

    IoOutDWord( address | offset, PCI_PORT_ADDR );
    IoOutWord( value, PCI_PORT_ADDR );
}


//
// PCIReadConfigDWord
//
// Input:
//  address : PCI device address
//  offset  : Offset address of register
//
// Return:
//  Register value in double word
//
// Description:
//  Read PCI configuration space register in double word
//
__u32 PCIReadConfigDWord( __u32 address, __u8 offset ) {

    IoOutDWord( address | offset , PCI_PORT_ADDR );
    return IoInDWord( PCI_PORT_DATA );
}


//
// PCIWriteConfigDWord
//
// Input:
//  address : PCI device address
//  offset  : Offset address of register
//  value   : Value to write
//
// Return:
//  Register value in double word
//
// Description:
//  Write double word to PCI configuration space register
//
void PCIWriteConfigDWord( __u32 address, __u8 offset, __u32 value ) {

    IoOutDWord( address | offset, PCI_PORT_ADDR );
    IoOutDWord( value, PCI_PORT_ADDR );
}


//
// PCICalBaseAddr
//
// Input:
//  bus     : PCI bus number
//  dev     : PCI device number
//  func    : PCI function number
//
// Return:
//  PCI base address
//
// Description:
//  Calculate PCI base address by bus, device, and function numbers
//
__u32 PCICalBaseAddr( __u8 bus, __u8 dev, __u8 func ) {

    return PCI_ENABLE_BIT | (bus << 16) | ((dev & 0x1f) << 11) | ((func & 0x07) << 8);
}


//
// PCIDetectDevice
//
// Input:
//  None
//
// Return:
//  None
//
// Description:
//  Scaning all PCI devices on the bus
//
void PCIDetectDevice( void ) {

    __u32 value;
    __u16 bus;
    __u8 dev, func;


    for( bus = 0 ; bus <= PCI_BUS_MAX ; bus++ ) {
    
        for( dev = 0 ; dev <= PCI_DEV_MAX ; dev++ ) {
        
            for( func = 0 ; func <= PCI_FUN_MAX ; func++ ) {
            
                value = PCIReadConfigDWord( PCICalBaseAddr( bus, dev, func ), 0 );
                if( value != 0xffffffff ) {
                
                    TcPrint( "PCI Bus : %4X, Dev : %4X, Func : %4X, Vid = %4X, Did = %4X\n"
                                    , bus, dev, func, (value & 0xffff), ((value >> 16) & 0xffff) );
                }
            }
        }
    }
}


