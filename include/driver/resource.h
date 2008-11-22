/*
 * Copyright (C) 2006 - 2008 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * File: resource.h
 * Description:
 *  OluxOS resource driver header file
 *
 */


#define     SMBIOS_BASE     0x000f0000
#define     SMBIOS_ANCH     "_SM_"


struct smbios_entry {

    u32   AnchorStr;
    u8    Checksum;
    u8    Length;
    u8    MajorVer;
    u8    MinorVer;
    u16   MaxSize;
    u8    Revision;
    u8    FormattedArea[ 5 ];
    u8    ImdAnchorStr[ 5 ];
    u8    ImdChecksum[ 5 ];
    u16   STblLength;
    u32   STblAddr;
    u16   NRStruct;
    u8    BCDRev;
};


s8 ChkCpuidSup( void );
s8 ChkMSRSup( void );
u8 ChkSMBIOSSup( void );


