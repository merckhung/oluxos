/*
 * Copyright (C) 2006 - 2007 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * @OLUXORG_LICENSE_HEADER_START@
 * @OLUXORG_LICENSE_HEADER_END@
 *
 */


#define     KBCNT_PORT      0x60
#define     KB8042_PORT     0x64


void KbInitKeyboard( void );
void KbIntHandler( __u8 irqnum );

void Kb8042SendCmd( __u8 cmd );
void KbSendCmd( __u8 cmd );


