/*
 * Copyright (C) 2006 - 2008 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * File: ksh.h
 * Description:
 *	OluxOS Kernel shell header file
 *
 */


//
// Definitions
//
#define KSH_PROMPT				"OluxOS > "


enum {

    OLUX_CMD_UNKNOWN = 0,
    OLUX_CMD_HELP,
    OLUX_CMD_LSPCI,
    OLUX_CMD_IDE,
	OLUX_CMD_MENU,
};



//
// Structures
//
typedef struct _CmdPair {

    s8                  *CmdStr;
    s32                 CmdCode;

} CmdPair;



//
// Prototypes
//
void KshHandleCmd( void );
u32 KshParseCmd( s8 *CmdBuf, s8 **Param );
void KshExecCmd( s32 CmdCode, s8 *Param );



