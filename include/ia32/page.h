/*
 * Copyright (C) 2007 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * @OLUXORG_LICENSE_HEADER_START@
 * @OLUXORG_LICENSE_HEADER_END@
 *
 */
#define     MEM_SIZE    32 * 1024 * 1024
#define     PTENUM      8 * 1024
#define     PDENUM      8
#define     PAGEADDR    0x200000


void ia32_MmPageInit( void );
