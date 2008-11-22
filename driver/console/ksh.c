/*
 * Copyright (C) 2007 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * File: ksh.c
 * Description:
 *	OluxOS Kernel Shell
 *
 */
#include <types.h>
#include <clib.h>
#include <version.h>
#include <ia32/platform.h>
#include <ia32/interrupt.h>
#include <ia32/io.h>
#include <ia32/debug.h>
#include <ia32/page.h>
#include <driver/kbd.h>
#include <driver/console.h>
#include <driver/pci.h>
#include <driver/ide.h>
#include <driver/menu.h>
#include <driver/ksh.h>


static s8 InputBuf[ LEN_CMDBUF ];
static u32 InputIndex;
static s8 *Param = NULL;
static s8 SectorBuf[ IDE_SZ_SECTOR ];


enum {

	OLUX_CMD_UNKNOWN = 0,
	OLUX_CMD_HELP,
	OLUX_CMD_REBOOT,
	OLUX_CMD_LSPCI,
	OLUX_CMD_IDE,
	OLUX_CMD_MENU,
	OLUX_CMD_CLRSCR,
	OLUX_CMD_E820,
	OLUX_CMD_MEM,
};


static CmdPair Cmds[] = {

	{ "mem",			OLUX_CMD_MEM },
	{ "e820",			OLUX_CMD_E820 },
	{ "clrscr",			OLUX_CMD_CLRSCR },
	{ "menu",           OLUX_CMD_MENU },
	{ "ide",			OLUX_CMD_IDE },
	{ "lspci",			OLUX_CMD_LSPCI },
	{ "reboot",			OLUX_CMD_REBOOT },
    { "help",			OLUX_CMD_HELP },
    { "",				OLUX_CMD_UNKNOWN },
};



void KshStart( void ) {

	// Initializing
    InputIndex = 0;
	TcPrint( "\n"KSH_PROMPT );

	for(;;);
}



void KshInsertCharacter( KbdAsciiPair *in ) {


	if( in->ScanCode != KEY_ENTER ) {

		// Get character
		InputBuf[ InputIndex ] = in->AsciiCode;
		InputIndex++;
		TcPrint( "%c", in->AsciiCode );
	}
	else {

		TcPrint( "\n" );
		KshHandleCmd();
	}
}



bool KshParseOneParameter( s8 *buf, u32 *first ) {


    // Check length
    if( CbStrLen( buf ) > 10 ) {

        return FALSE;
    }


    // Check hex digits
    if( !((*buf == '0') && (*(buf + 1) == 'x')) ) {

        return FALSE;
    }


    *first = CbAsciiBufToBin( buf + 2 );
    return TRUE;
}



//
// KshHandleCmd
//
// Input:
//	None
//
// Return:
//	None
//
// Description:
//	Handle Keyboard Command
//
void KshHandleCmd( void ) {

	u32 CmdCode;


	// Terminate string
	InputBuf[ InputIndex ] = 0;


	// Parse command string
	CmdCode = KshParseCmd( InputBuf, &Param );


	// Execute command
	KshExecCmd( CmdCode, Param );


	// Clear buffer
    InputIndex = 0;


	TcPrint( KSH_PROMPT );
}



u32 KshParseCmd( s8 *CmdBuf, s8 **Param ) {

    u32 i;
    s8 *var;


    // Handle parameter
    var = CbIndex( CmdBuf, ' ' );
    if( var ) {

        (*var) = 0;
        var++;
        *Param = var;
    }


    for( i = 0 ; Cmds[ i ].CmdStr[ 0 ] ; i++ ) {

        if( !CbStrCmpL( CmdBuf, Cmds[ i ].CmdStr ) ) {

            return Cmds[ i ].CmdCode;
        }
    }


    *Param = NULL;
    return OLUX_CMD_UNKNOWN;
}



void KshExecCmd( s32 CmdCode, s8 *Param ) {


    switch( CmdCode ) {


        case OLUX_CMD_LSPCI:


			// Scan Pci
			PciDetectDevice();
            break;


		case OLUX_CMD_IDE:

			// Read a sector
#if 0
			if( KshParseOneParameter( Param, &lba ) == FALSE ) {

				IDEReadSector( lba, SectorBuf );
			}
#endif
			IDEReadSector( 0, SectorBuf );
			break;


#ifdef CONFIG_MENU
		case OLUX_CMD_MENU:

			// BIOS like menu
			MenuInit();
			break;
#endif


		case OLUX_CMD_CLRSCR:

			// Clear screen
			TcClear();
			break;

		
		case OLUX_CMD_E820:

			// Display E820 information
			MmShowE820Info();
			break;


		case OLUX_CMD_MEM:

			// Dump memory
			//KshDumpMemory( buf, 0x10, 0x0 );
			break;


		case OLUX_CMD_REBOOT:

			// Hard reboot by PCI reset
			//TcPrint( "Reboot the system......\n" );
			IoOutByte( 0x06, 0xCF9 );
			break;


        case OLUX_CMD_HELP:

			// Print usage
			KshUsage();
            break;


        default:

			KshUsage();
            break;
    }
}



void KshUsage( void ) {

	TcPrint( COPYRIGHT_STR"\n" );
	TcPrint( "OluxOS Kernel Shell, version "KRN_VER"\n\n" );

	TcPrint( "  lspci          - Show all PCI devices\n");
	TcPrint( "  ide <LBA>      - Read a sector from IDE disk\n" );
#ifdef CONFIG_MENU
	TcPrint( "  menu           - BIOS like menu\n" );
#endif
	TcPrint( "  e820           - Show E820 memory population\n" );
	TcPrint( "  mem <ADDR/LEN> - Dump memory\n" );

	TcPrint( "  clrscr         - Clear screen\n" );
	TcPrint( "  reboot         - Reboot\n");
	TcPrint( "  help           - Display this message\n\n" );
}



#if 0
void KshDumpMemory( u8 *Data, u32 Length, u32 BaseAddr ) {

    u32 i, j;
    u32 c;


    TcPrint( "\n\n== Dump Memory Start ==\n\n" );
    TcPrint( " Address | 00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F|   ASCII DATA   \n" );
    TcPrint( "---------------------------------------------------------------------------\n" );


    for( i = 0 ; i <= Length ; i++ ) {


        if( !(i % 16) ) {


            if( (i > 15) ) {

                for( j = i - 16 ; j < i ; j++ ) {

                    c = *(Data + j);
                    if( ((c >= '!') && (c <= '~')) ) {

                        TcPrint( "%c", c );
                    }
                    else {

                        TcPrint( "." );
                    }
                }
            }


            if( i ) {

                TcPrint( "\n" );
            }


            if( i == Length ) {

                break;
            }


            TcPrint( "%8.8X : ", i + BaseAddr );
        }


        TcPrint( "%2.2X ", *(Data + i) & 0xFF );
    }


    TcPrint( "\n---------------------------------------------------------------------------\n" );
    TcPrint( "\n== Dump Memory End ==\n\n" );
}
#endif


