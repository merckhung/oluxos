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

TARGET				=	krnimg instkrn
CFLAGS				=	-O2 -Wall -Iinclude -I../include
LDFLAGS				=


all: $(TARGET)


krnimg: krnimg.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $<


instkrn: instkrn.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< libcomm.c


clean:
	rm -f *.o $(TARGET)


