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
#include <driver/console.h>
#include <driver/pci.h>
#include <ia32/debug.h>
#include <ia32/io.h>
#include <types.h>

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
uint8_t PciReadConfigByte(uint32_t address, uint8_t offset) {
  IoOutDWord(address | offset, PCI_PORT_ADDR);
  return IoInByte(PCI_PORT_DATA);
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
void PciWriteConfigByte(uint32_t address, uint8_t offset, uint8_t value) {
  IoOutDWord(address | offset, PCI_PORT_ADDR);
  IoOutByte(value, PCI_PORT_ADDR);
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
uint16_t PciReadConfigWord(uint32_t address, uint8_t offset) {
  IoOutDWord(address | offset, PCI_PORT_ADDR);
  return IoInWord(PCI_PORT_DATA);
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
void PciWriteConfigWord(uint32_t address, uint8_t offset, uint16_t value) {
  IoOutDWord(address | offset, PCI_PORT_ADDR);
  IoOutWord(value, PCI_PORT_ADDR);
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
uint32_t PciReadConfigDWord(uint32_t address, uint8_t offset) {
  IoOutDWord(address | offset, PCI_PORT_ADDR);
  return IoInDWord(PCI_PORT_DATA);
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
void PciWriteConfigDWord(uint32_t address, uint8_t offset, uint32_t value) {
  IoOutDWord(address | offset, PCI_PORT_ADDR);
  IoOutDWord(value, PCI_PORT_ADDR);
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
uint32_t PciCalBaseAddr(uint16_t bus, uint8_t dev, uint8_t func) {
  return PCI_ENABLE_BIT | (((uint32_t)bus) << 16) |
         ((((uint32_t)dev) & 0x1F) << 11) | ((((uint32_t)func) & 0x07) << 8);
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
void PciDetectDevice(void) {
  uint32_t value;
  uint16_t bus;
  uint8_t dev, func;

  // Disabled to isolate crash
}
