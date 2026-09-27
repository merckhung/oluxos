/*
 * Copyright (C) 2006 - 2008 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * File: bios.h
 * Description:
 * 	None
 *
 */

//
// Definitions
//
#define HDD_READ 0x02
#define HDD_WRITE 0x03
#define HDD_READ_LONG 0x0A
#define HDD_WRITE_LONG 0x0B
#define HDD_READ_EXT 0x42
#define HDD_WRITE_EXT 0x43

#define HDD_DRIVE_FIRST 0x80
#define HDD_DRIVE_SECOND 0x81

#define HDD_MBR_SIGNATURE 0xAA55
#define HDD_SIZE_SECTOR 512

#define HDD_MAX_CYLINDER 0x3FF
#define HDD_MAX_HEAD 0xFF
#define HDD_MAX_SECTOR 0x3F

#define E820_COUNT 0x9000
#define E820_BASE 0x9004
#define E820_FUNC 0xE820
#define E820_MAGIC 0x534D4150
#define E820_SZ_RECORD 24

//
// Structures
//
#ifndef __ASSEMBLER__
typedef struct PACKED _DiskAddrPacket {
  uint8_t PacketSize;
  uint8_t Reserved0;
  uint8_t NrSectors;
  uint8_t Reserved1;
  uint16_t PacketOff;
  uint16_t PacketSeg;
  uint64_t LBA;
  uint64_t FlatAddr;

} DiskAddrPacket;
#endif
