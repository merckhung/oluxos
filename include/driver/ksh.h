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
#define LEN_CMDBUF				256



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
void KshStart( void );
void KshInsertCharacter( KbdAsciiPair *in );
void KshHandleCmd( void );
u32 KshParseCmd( s8 *CmdBuf, s8 **Param );
void KshExecCmd( s32 CmdCode, s8 *Param );
void KshUsage( void );
void KshDumpMemory( u8 *Data, u32 Length, u32 BaseAddr );


