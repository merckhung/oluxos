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
#include <ia32/platform.h>
#include <ia32/interrupt.h>
#include <ia32/io.h>
#include <ia32/debug.h>
#include <driver/kbd.h>
#include <driver/console.h>
#include <driver/pci.h>
#include <driver/ide.h>
#include <driver/menu.h>
#include <driver/ksh.h>


extern s8 InputBuf[ LEN_CMDBUF ];
extern u32 InputIndex;
static s8 *Param = NULL;


static CmdPair Cmds[] = {

	{ "menu",           OLUX_CMD_MENU },
	{ "ide",			OLUX_CMD_IDE },
	{ "lspci",			OLUX_CMD_LSPCI },
    { "help",			OLUX_CMD_HELP },
    { "",				OLUX_CMD_UNKNOWN },
};



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


	// Parse command string
	CmdCode = KshParseCmd( InputBuf, &Param );


	// Execute command
	KshExecCmd( CmdCode, Param );


	// Clear buffer
    InputIndex = 0;
    CbMemSet( InputBuf, 0, LEN_CMDBUF );


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


			// Show IDE
			IDEInit();
			break;


		case OLUX_CMD_MENU:

			// BIOS like menu
			MenuInit();
			break;


        case OLUX_CMD_HELP:


            TcPrint( "\nOluxOS shell command list:\n" );
            TcPrint( "  lspci       - Show all PCI devices\n" );
            TcPrint( "  ide         - Read a block from IDE disk\n" );
			TcPrint( "  menu        - BIOS like menu\n" );
            TcPrint( "  help        - Show this help\n" );
            TcPrint( "\n" );
            break;


        default:

            break;
    }
}



