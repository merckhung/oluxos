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
include ../scripts/rules.mk

TARGET				=	krnimg instkrn kdbger
CFLAGS				=	-O2 -Wall -Iinclude -I../include
LDFLAGS				=


all: $(TARGET)

krnimg: krnimg.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $<

instkrn: instkrn.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< libcomm.c

kdbger: kdbger.c
	$(CC) $(CFLAGS) $(LDFLAGS) -lpanel -o $@ $< packet.c dumpPanel.c utils.c

clean:
	rm -f *.o $(TARGET)


