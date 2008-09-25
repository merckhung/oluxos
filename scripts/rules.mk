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
kobjs-d				:=
kobjs-o				:=


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
$(foreach _sdir, $(BASE_SRCDIRS), $(shell find $(_sdir) -type d | grep -v "\.svn" | grep -v "CVS" | grep -v ".git"))
endef


define FindAllSubDirectoriesSlash
$(addsuffix /, $(call FindAllSubDirectories))
endef


define FindAllSubMakefiles
$(foreach _smak, $(call FindAllSubDirectories), $(wildcard $(_smak)/Makefile))
endef


define FindLocation
$(dir $(lastword $(MAKEFILE_LIST)))
endef


define FindSubMakefiles
$(foreach _sdir, $(BASE_SRCDIRS), $(wildcard $(_sdir)/Makefile))
endef


# 1) Find out all path of files
# 2) Separate Directories(kobjs-d) and Files(kobjs-o)
define IncAllMakefiles
$(eval \
	$(eval alldirs := $(call FindAllSubDirectories)) \
	$(eval alldirs_s := $(addsuffix /, $(alldirs))) \
	$(foreach _smak, $(call FindSubMakefiles), \
		$(eval \
			$(eval _tmp_kobjs-y := $(kobjs-y))
			$(eval kobjs-y := )
			$(eval include $(_smak))
			$(eval kobjs-y := $(sort $(addprefix $(call FindLocation), $(kobjs-y)) $(_tmp_kobjs-y)))
		)
	) \
	$(foreach _dchk, $(kobjs-y), \
		$(if $(findstring $(_dchk), $(alldirs)), kobjs-d += $(_dchk), \
			$(if $(findstring $(_dchk), $(alldirs_s)), kobjs-d += $(_dchk), kobjs-o += $(_dchk) ) )
	)
)
endef



