/*
 * Copyright (C) 2006 - 2008 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * File: pci.h
 * Description:
 *  OluxOS PCI driver header file
 *
 */

#define PCI_ENABLE_BIT 0x80000000
#define PCI_BUS_MAX 0x00FF
#define PCI_DEV_MAX 0x1F
#define PCI_FUN_MAX 0x07

#define PCI_PORT_ADDR 0x0CF8
#define PCI_PORT_DATA 0x0CFC

uint8_t PciReadConfigByte(uint32_t address, uint8_t offset);
void PciWriteConfigByte(uint32_t address, uint8_t offset, uint8_t value);
uint16_t PciReadConfigWord(uint32_t address, uint8_t offset);
void PciWriteConfigWord(uint32_t address, uint8_t offset, uint16_t value);
uint32_t PciReadConfigDWord(uint32_t address, uint8_t offset);
void PciWriteConfigDWord(uint32_t address, uint8_t offset, uint32_t value);
uint32_t PciCalBaseAddr(uint16_t bus, uint8_t dev, uint8_t func);

void PciDetectDevice(void);
