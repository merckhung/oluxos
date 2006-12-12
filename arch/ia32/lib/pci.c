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
// ia32_PCIReadConfigByte
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
__u8 ia32_PCIReadConfigByte( __u32 address, __u8 offset ) {

    ia32_IoOutDWord( address | offset , PCI_PORT_ADDR );
    return ia32_IoInByte( PCI_PORT_DATA );
}


//
// ia32_PCIWriteConfigByte
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
void ia32_PCIWriteConfigByte( __u32 address, __u8 offset, __u8 value ) {

    ia32_IoOutDWord( address | offset, PCI_PORT_ADDR );
    ia32_IoOutByte( value, PCI_PORT_ADDR );
}


//
// ia32_PCIReadConfigWord
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
__u16 ia32_PCIReadConfigWord( __u32 address, __u8 offset ) {

    ia32_IoOutDWord( address | offset , PCI_PORT_ADDR );
    return ia32_IoInWord( PCI_PORT_DATA );
}


//
// ia32_PCIWriteConfigWord
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
void ia32_PCIWriteConfigWord( __u32 address, __u8 offset, __u16 value ) {

    ia32_IoOutDWord( address | offset, PCI_PORT_ADDR );
    ia32_IoOutWord( value, PCI_PORT_ADDR );
}


//
// ia32_PCIReadConfigDWord
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
__u32 ia32_PCIReadConfigDWord( __u32 address, __u8 offset ) {

    ia32_IoOutDWord( address | offset , PCI_PORT_ADDR );
    return ia32_IoInDWord( PCI_PORT_DATA );
}


//
// ia32_PCIWriteConfigDWord
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
void ia32_PCIWriteConfigDWord( __u32 address, __u8 offset, __u32 value ) {

    ia32_IoOutDWord( address | offset, PCI_PORT_ADDR );
    ia32_IoOutDWord( value, PCI_PORT_ADDR );
}


//
// ia32_PCICalBaseAddr
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
__u32 ia32_PCICalBaseAddr( __u8 bus, __u8 dev, __u8 func ) {

    return PCI_ENABLE_BIT | (bus << 16) | ((dev & 0x1f) << 11) | ((func & 0x07) << 8);
}


//
// ia32_PCIDetectDevice
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
void ia32_PCIDetectDevice( void ) {

    __u32 value;
    __u16 bus;
    __u8 dev, func;


    for( bus = 0 ; bus <= PCI_BUS_MAX ; bus++ ) {
    
        for( dev = 0 ; dev <= PCI_DEV_MAX ; dev++ ) {
        
            for( func = 0 ; func <= PCI_FUN_MAX ; func++ ) {
            
                value = ia32_PCIReadConfigDWord( ia32_PCICalBaseAddr( bus, dev, func ), 0 );
                if( value != 0xffffffff ) {
                
                    ia32_TcPrint( "PCI Bus : %4X, Dev : %4X, Func : %4X, Vid = %4X, Did = %4X\n"
                                    , bus, dev, func, (value & 0xffff), ((value >> 16) & 0xffff) );
                }
            }
        }
    }
}


