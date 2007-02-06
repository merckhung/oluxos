/*
 * Copyright (C) 2006 - 2007 Olux Organization All rights reserved.
 * Author: Merck Hung <merck@olux.org>
 *
 * @OLUXORG_LICENSE_HEADER_START@
 * @OLUXORG_LICENSE_HEADER_END@
 *
 * multiboot.S -- OluxOS boot code
 *
 * This is 32bit mode setup code after multiboot loader
 *  1. setup global segment descriptors
 *  2. setup stack base and pointer
 *  3. jump to C kernel entry
 * 
 */


.file       "boot.S"


MULTIBOOT_MAGIC=0x1badb002
MULTIBOOT_FLAGS=0x00000003


/******************************************************************************
 * Code Segment
 */
.text
.global     _multiboot


_multiboot:
    jmp     _start


.align  4
    .long   MULTIBOOT_MAGIC
    .long   MULTIBOOT_FLAGS
    .long   -(MULTIBOOT_MAGIC + MULTIBOOT_FLAGS)


_start:
    jmp     _setup


