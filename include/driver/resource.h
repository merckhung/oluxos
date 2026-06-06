/*
 * Copyright (C) 2006 - 2008 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * File: resource.h
 * Description:
 *  OluxOS resource driver header file
 *
 */

#define SMBIOS_BASE 0x000f0000
#define SMBIOS_ANCH "_SM_"

struct smbios_entry {
  uint32_t AnchorStr;
  uint8_t Checksum;
  uint8_t Length;
  uint8_t MajorVer;
  uint8_t MinorVer;
  uint16_t MaxSize;
  uint8_t Revision;
  uint8_t FormattedArea[5];
  uint8_t ImdAnchorStr[5];
  uint8_t ImdChecksum[5];
  uint16_t STblLength;
  uint32_t STblAddr;
  uint16_t NRStruct;
  uint8_t BCDRev;
};

int8_t ChkCpuidSup(void);
int8_t ChkMSRSup(void);
uint8_t ChkSMBIOSSup(void);
