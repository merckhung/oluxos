/*
 * Copyright (C) 2006 - 2007 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * @OLUXORG_LICENSE_HEADER_START@
 * @OLUXORG_LICENSE_HEADER_END@
 *
 */


#define     SMBIOS_BASE     0xfff00000
#define     SMBIOS_ANCH     "_ms_"


struct smbios_entry {

    __u32   AnchorStr;
    __u8    Checksum;
    __u8    Length;
    __u8    MajorVer;
    __u8    MinorVer;
    __u16   MaxSize;
    __u8    Revision;
    __u8    FormattedArea[ 5 ];
    __u8    ImdAnchorStr[ 5 ];
    __u8    ImdChecksum[ 5 ];
    __u16   STblLength;
    __u32   STblAddr;
    __u16   NRStruct;
    __u8    BCDRev;
};


__s8 ChkCpuidSup( void );
__s8 ChkMSRSup( void );
__u8 ChkSMBIOSSup( void );


