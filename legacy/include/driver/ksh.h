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
#define KSH_PROMPT "OluxOS > "
#define LEN_CMDBUF 256

//
// Structures
//
typedef struct _CmdPair {
  int8_t* CmdStr;
  int32_t CmdCode;

} CmdPair;

//
// Prototypes
//
void KshStart(void);
void KshInsertCharacter(KbdAsciiPair* in);
bool KshParseOneParameter(int8_t* buf, uint32_t* first);
void KshHandleCmd(void);
uint32_t KshParseCmd(int8_t* CmdBuf, int8_t** Param);
void KshExecCmd(int32_t CmdCode, int8_t* Param);
void KshUsage(void);
void KshDumpMemory(uint8_t* Data, uint32_t Length, uint32_t BaseAddr);
