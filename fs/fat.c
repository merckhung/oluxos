/*
 * Copyright (C) 2006 - 2007 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * @OLUXORG_LICENSE_HEADER_START@
 * @OLUXORG_LICENSE_HEADER_END@
 *
 * fat.c -- OluxOS FAT file system driver
 *
 */
#include <types.h>
#include <clib.h>
#include <ia32/debug.h>
#include <driver/ide.h>
#include <fs/fat.h>


void FsFatInit( void ) {

    __s8 buf[ 512 ], tmp[ 12 ];
    __s32 i;
    FsFatBPB_t *p;


    ReadSector( 0 );
    ReadData( buf );
    p = (FsFatBPB_t *)buf;


#if 1
    for( i = 0 ; i < 512 ; i++ ) {

        if( !( i % 26 ) ) {
        
            DbgPrint( "\n" );
        }

        DbgPrint( "%2X ", *(buf + i) );
    }
#endif


#if 1
    DbgPrint( "\n" );

    strncpy( tmp, p->BS_OEMName, sizeof( p->BS_OEMName ) );
    tmp[ sizeof( p->BS_OEMName ) ] = 0;
    DbgPrint( "BS_OEMName     = %s\n", p->BS_OEMName );
    DbgPrint( "BPB_BytePerSec = 0x%4X\n", p->BPB_BytePerSec );
    DbgPrint( "BPB_SecPerClus = 0x%2X\n", p->BPB_SecPerClus );
    DbgPrint( "BPB_RsvdSecCnt = 0x%4X\n", p->BPB_RsvdSecCnt );
    DbgPrint( "BPB_NumFATs    = 0x%2X\n", p->BPB_NumFATs );
    DbgPrint( "BPB_RootEntCnt = 0x%4X\n", p->BPB_RootEntCnt );
    DbgPrint( "BPB_TotSec16   = 0x%4X\n", p->BPB_TotSec16 );
    DbgPrint( "BPB_Media      = 0x%2X\n", p->BPB_Media );
    DbgPrint( "BPB_FATSz16    = 0x%4X\n", p->BPB_FATSz16 );
    DbgPrint( "BPB_SecPerTrk  = 0x%4X\n", p->BPB_SecPerTrk );
    DbgPrint( "BPB_NumHeads   = 0x%4X\n", p->BPB_NumHeads );
    DbgPrint( "BPB_HiddSec    = 0x%8X\n", p->BPB_HiddSec );
    DbgPrint( "BPB_TotSec32   = 0x%8X\n", p->BPB_TotSec32 );


    if( p->BPB_FATSz16 ) {
    
        FsFatBPBFat16_t *p16 = (FsFatBPBFat16_t *)(buf + 36);
    
        DbgPrint( "BS_DrvNum      = 0x%2X\n", p16->BS_DrvNum );
        DbgPrint( "BS_Reserved1   = 0x%2X\n", p16->BS_Reserved1 );
        DbgPrint( "BS_BootSig     = 0x%2X\n", p16->BS_BootSig );

        if( p16->BS_BootSig == 0x29 ) {

            DbgPrint( "BS_VolID       = 0x%8X\n", p16->BS_VolID );

            strncpy( tmp, p16->BS_VolLab, sizeof( p16->BS_VolLab ) );
            tmp[ sizeof( p16->BS_VolLab ) ] = 0;
            DbgPrint( "BS_VolLab      = %s\n", tmp );

            strncpy( tmp, p16->BS_FilSysType, sizeof( p16->BS_FilSysType ) );
            tmp[ sizeof( p16->BS_FilSysType ) ] = 0;
            DbgPrint( "BS_FilSysType  = %s\n", tmp );
        }
    }
    else if( p->BPB_TotSec32 ) {
    
        // FIXME:
        // FsFatBPBFat32_t *p32 = (FsFatBPBFat32_t *)(p + 36);
    }
#endif
}


