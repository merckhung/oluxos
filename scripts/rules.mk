################################################################################
#
# Copyright (C) 2006 - 2008 Olux Organization All rights reserved.
# Authors: Merck Hung <merck@olux.org>
#
# File: rules.mk
# Description:
#   None
#
################################################################################
#
# Variables
#
AS                  :=	$(CROSS_COMPILE)as
AR                  :=	$(CROSS_COMPILE)ar
CC                  :=	$(CROSS_COMPILE)gcc
CPP                 :=	$(CC) -E
LD                  :=	$(CROSS_COMPILE)ld
NM                  :=	$(CROSS_COMPILE)nm
OBJCOPY             :=	$(CROSS_COMPILE)objcopy
OBJDUMP             :=	$(CROSS_COMPILE)objdump
RANLIB              :=	$(CROSS_COMPILE)ranlib
READELF             :=	$(CROSS_COMPILE)readelf
SIZE                :=	$(CROSS_COMPILE)size
STRINGS             :=	$(CROSS_COMPILE)strings
STRIP               :=	$(CROSS_COMPILE)strip
ECHO				:=	@echo
MV                  :=	@mv
DD                  :=	@dd


CROSS_COMPILE       ?=


BASE_SRCDIRS		:=	arch driver fs krn lib
kobjs-y				:=


QUIET               ?=  QUIET_

QUIET_CMD_AS        ?=  AS      $@
      CMD_AS        ?=  $(CC) $(ASFLAGS) $(CFLAGS) -c -o $(<D)/$@ $<

QUIET_CMD_CC        ?=  CC      $@
      CMD_CC        ?=  $(CC) $(CFLAGS) -c -o $(<D)/$@ $<

QUIET_CMD_LD        ?=  LD      $@
      CMD_LD        ?=  $(LD) $(LDFLAGS) -o $@ $(shell cat $(OBJECTLIST)) > $@.map



#
# Make rules
#
%.o: %.S
	$(ECHO) '   $($(QUIET)CMD_AS)'
	$(CMD_AS)
	$(ECHO) -n '$(<D)/$@ ' >> $(OBJECTLIST)


%.o: %.c
	$(ECHO) '   $($(QUIET)CMD_CC)'
	$(CMD_CC)
	$(ECHO) -n '$(<D)/$@ ' >> $(OBJECTLIST)



#
# Definitions
#
define FindAllSubDirectories
$(foreach _sdir, $(BASE_SRCDIRS), $(shell find $(_sdir) -type d))
endef


define FindAllSubMakefiles
$(foreach _smak, $(call FindAllSubDirectories), $(wildcard $(_smak)/Makefile))
endef


define FindAllSubMakefiles2
$(foreach _sdir, $(BASE_SRCDIRS), $(wildcard $(_sdir)/Makefile))
endef


define FindLocation
$(dir $(lastword $(KEFILE_LIST)))
endef


define IncAllMakefiles
	include $(call FindAllSubMakefiles2)
endef


