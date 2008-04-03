/*
 * Copyright (C) 2006 - 2008 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * @OLUXORG_LICENSE_HEADER_START@
 * @OLUXORG_LICENSE_HEADER_END@
 *
 */


typedef struct {

    s8    BS_jmpBoot[ 3 ];
    s8    BS_OEMName[ 8 ];
    u16   BPB_BytePerSec;
    u8    BPB_SecPerClus;
    u16   BPB_RsvdSecCnt;
    u8    BPB_NumFATs;
    u16   BPB_RootEntCnt;
    u16   BPB_TotSec16;
    u8    BPB_Media;
    u16   BPB_FATSz16;
    u16   BPB_SecPerTrk;
    u16   BPB_NumHeads;
    u32   BPB_HiddSec;
    u32   BPB_TotSec32;

} __attribute__ ((packed)) FsFatBPB_t;


typedef struct {

    u8    BS_DrvNum;
    u8    BS_Reserved1;
    u8    BS_BootSig;
    u32   BS_VolID;
    s8    BS_VolLab[ 11 ];
    s8    BS_FilSysType[ 8 ];

} __attribute__ ((packed)) FsFatBPBFat16_t;


typedef struct {

    u32   BPB_FATSz32;
    u16   BPB_ExtFlags;
    u16   BPB_FSVer;
    u32   BPB_RootClus;
    u16   BPB_FSInfo;
    u16   BPB_BkBootSec;
    s8    BPB_Reserved[ 12 ];
    u8    BS_DrvNum;
    u8    BS_Reserved1;
    u8    BS_BootSig;
    u32   BS_VolID;
    s8    BS_VolLab[ 11 ];
    s8    BS_FilSysType[ 8 ];

} __attribute__ ((packed)) FsFatBPBFat32_t;


void FsFatInit( void );


