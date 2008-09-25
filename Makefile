################################################################################
#
# Copyright (C) 2006 - 2008 Olux Organization All rights reserved.
# Authors: Merck Hung <merck@olux.org>
#
# File: Makefile
# Description:
#   None
#
################################################################################
include scripts/rules.mk
include .config
$(call IncAllMakefiles)


MAJOR_VERSION		:=	0
MINOR_VERSION 		:=	1
EXTRA_VERSION		:=


ASFLAGS				=	-D__ASM__
CFLAGS              =   -Wall -Wstrict-prototypes -O2 -fomit-frame-pointer -nostdinc -isystem include -DKERNEL_DEBUG
LDFLAGS             =	-cref -M -s -N
MAKEFLAGS			+=	--no-print-directory --no-builtin-rules --no-builtin-variables --quiet


ARCH				:=	$(shell uname -m | sed -e s/i.86/ia32/)
BOOTOBJS			:=	arch/$(ARCH)/boot/boot.o arch/$(ARCH)/boot/info.o arch/$(ARCH)/boot/pm.o
KRNOBJS				:=	$(kobjs-o)


IMGSIZE				=	1474560
SYSIMG				=	OluxOS.img
KRNNAME				=	OluxOS.krn
KRNMAIN				=	kernel
BOOTSECT			=	bootsect
UTILS				=	utils


.PHONY:	$(UTILS)


all: $(BOOTSECT) $(KRNMAIN) $(KRNNAME)


$(KRNNAME):
	$(MAKE) -C $(UTILS)
	$(UTILS)/krnimg -b $(BOOTSECT) -k $(KRNMAIN) -o $(KRNNAME)


$(KRNMAIN): $(KRNOBJS)
	$(ECHO) '   $($(QUIET)CMD_LD)'
	$(CMD_LD) -T arch/$(ARCH)/kernel.lds


$(BOOTSECT): $(BOOTOBJS)
	$(ECHO) '   $($(QUIET)CMD_LD)'
	$(CMD_LD) -T arch/$(ARCH)/boot/boot.lds


clean:
	$(MAKE) -C $(UTILS) clean
	$(RM) $(BOOTOBJS) $(KRNOBJS) $(BOOTSECT) $(KRNMAIN) $(KRNNAME) $(SYSIMG) *.map *.lst


emu:
	$(DD) if=/dev/zero of=$(SYSIMG) bs=$(IMGSIZE) count=1
	$(DD) if=$(KRNNAME) of=$(SYSIMG) bs=$(IMGSIZE) count=1 conv=notrunc
	qemu -hda $(SYSIMG) -m 256 -no-kqemu -s -serial /dev/tty39


over: clean all emu


