MAJOR_VERSION		=	0
MINOR_VERSION 		=	1
EXTRA_VERSION		=


AS                  =   $(CROSS_COMPILE)as
AR                  =   $(CROSS_COMPILE)ar
CC                  =   $(CROSS_COMPILE)gcc
CPP                 =   $(CC) -E
LD                  =   $(CROSS_COMPILE)ld
NM                  =   $(CROSS_COMPILE)nm
OBJCOPY             =   $(CROSS_COMPILE)objcopy
OBJDUMP             =   $(CROSS_COMPILE)objdump
RANLIB              =   $(CROSS_COMPILE)ranlib
READELF             =   $(CROSS_COMPILE)readelf
SIZE                =   $(CROSS_COMPILE)size
STRINGS             =   $(CROSS_COMPILE)strings
STRIP               =   $(CROSS_COMPILE)strip
ECHO				=	@echo


CROSS_COMPILE       =


ASFLAGS				=
CFLAGS              =   -Wall -Wstrict-prototypes -O2 -fomit-frame-pointer -nostdinc -isystem include
LDFLAGS             =	-cref -M -s -N -T arch/$(ARCH)/multiboot/multiboot.lds
MAKEFLAGS			+=	--no-print-directory --no-builtin-rules --no-builtin-variables --quiet


QUIET				?=	QUIET_

QUIET_CMD_AS		?=	AS		$@
	  CMD_AS		?=	$(AS)	-o $(<D)/$@ $<

QUIET_CMD_CC		?=	CC		$@
	  CMD_CC		?=	$(CC) $(CFLAGS) -c -o $(<D)/$@ $<

QUIET_CMD_LD		?=	LD		$@
	  CMD_LD		?=	$(LD) $(LDFLAGS) -o $@ $(shell cat object.lst) > $@.map


ARCH				:=	$(shell uname -m | sed -e s/i.86/ia32/)
VPATH				=	arch/$(ARCH)/kernel:arch/$(ARCH)/lib:arch/$(ARCH)/mm:arch/$(ARCH)/multiboot:lib
OBJECTS				=	string.o console.o ia32_krn.o interrupt.o int_handler.o debug.o io.o kbd.o pci.o page.o
OBJECTS				+=	multiboot.o timer.o
OBJECTLIST			=	object.lst


OluxOS.krn: $(OBJECTS)
	$(ECHO) '   $($(QUIET)CMD_LD)'
	$(CMD_LD)


%.o: %.S
	$(ECHO) '   $($(QUIET)CMD_AS)'
	$(CMD_AS)
	$(ECHO) -n '$(<D)/$@ ' >> $(OBJECTLIST)


%.o: %.c
	$(ECHO) '   $($(QUIET)CMD_CC)'
	$(CMD_CC)
	$(ECHO) -n '$(<D)/$@ ' >> $(OBJECTLIST)


clean:
	$(RM) -f $(shell cat $(OBJECTLIST) 2> /dev/null)
	$(RM) *.krn *.map *.img *.lst
	$(MAKE) -C image clean


img:
	$(MAKE) -C image all


emu:
	sudo modprobe kqemu
	qemu -fda OluxOS.img -m 256


over: clean OluxOS.krn img emu


