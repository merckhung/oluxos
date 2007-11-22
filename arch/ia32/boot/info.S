/*
 * Copyright (C) 2006 - 2007 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * @OLUXORG_LICENSE_HEADER_START@
 * @OLUXORG_LICENSE_HEADER_END@
 *
 *
 * info.S -- OluxOS code for getting BIOS information
 *
 */

.file       "info.S"


/******************************************************************************
 * Code Segment
 */
.text
.code16
.global     _info_start


_info_start:
    jmp     _pm_start


