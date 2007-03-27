/*
 * Copyright (C) 2006 - 2007 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * @OLUXORG_LICENSE_HEADER_START@
 * @OLUXORG_LICENSE_HEADER_END@
 *
 */


typedef struct {

    __s8    BS_jmpBoot[ 3 ];
    __s8    BS_OEMName[ 8 ];
    __u16   BPB_BytePerSec;
    __u8    BPB_SecPerClus;
    __u16   BPB_RsvdSecCnt;
    __u8    BPB_NumFATs;
    __u16   BPB_RootEntCnt;
    __u16   BPB_TotSec16;
    __u8    BPB_Media;
    __u16   BPB_FATSz16;
    __u16   BPB_SecPerTrk;
    __u16   BPB_NumHeads;
    __u32   BPB_HiddSec;
    __u32   BPB_TotSec32;

} FsFatBPB_t;


typedef struct {

    __u8    BS_DrvNum;
    __u8    BS_Reserved1;
    __u8    BS_BootSig;
    __u32   BS_VolID;
    __s8    BS_VolLab[ 11 ];
    __s8    BS_FilSysType[ 8 ];

} FsFatBPBFat16_t;


typedef struct {

    __u32   BPB_FATSz32;
    __u16   BPB_ExtFlags;
    __u16   BPB_FSVer;
    __u32   BPB_RootClus;
    __u16   BPB_FSInfo;
    __u16   BPB_BkBootSec;
    __s8    BPB_Reserved[ 12 ];
    __u8    BS_DrvNum;
    __u8    BS_Reserved1;
    __u8    BS_BootSig;
    __u32   BS_VolID;
    __s8    BS_VolLab[ 11 ];
    __s8    BS_FilSysType[ 8 ];

} FsFatBPBFat32_t;


void FsFatInit( void );


