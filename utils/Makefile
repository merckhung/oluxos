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
include ../scripts/make.tmpl

TARGET				=	krnimg
CFLAGS				=	-O2 -Wall -I../include
LDFLAGS				=


all:
	$(CC) $(CFLAGS) $(LDFLAGS) -o $(TARGET) $(TARGET).c


clean:
	rm -f *.o $(TARGET)


