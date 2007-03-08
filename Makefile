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


ASFLAGS				=	-D__ASM__
CFLAGS              =   -Wall -Wstrict-prototypes -O2 -fomit-frame-pointer -nostdinc -isystem include -DKERNEL_DEBUG
LDFLAGS             =	-cref -M -s -N -T arch/$(ARCH)/multiboot/multiboot.lds
MAKEFLAGS			+=	--no-print-directory --no-builtin-rules --no-builtin-variables --quiet


QUIET				?=	QUIET_

QUIET_CMD_AS		?=	AS		$@
	  CMD_AS		?=	$(CC) $(ASFLAGS) $(CFLAGS) -c -o $(<D)/$@ $<

QUIET_CMD_CC		?=	CC		$@
	  CMD_CC		?=	$(CC) $(CFLAGS) -c -o $(<D)/$@ $<

QUIET_CMD_LD		?=	LD		$@
	  CMD_LD		?=	$(LD) $(LDFLAGS) -o $@ $(shell cat object.lst) > $@.map


ARCH				:=	$(shell uname -m | sed -e s/i.86/ia32/)
VPATH				=	arch/$(ARCH)/kernel:arch/$(ARCH)/component:arch/$(ARCH)/lib:arch/$(ARCH)/mm:arch/$(ARCH)/multiboot
VPATH				+=	:lib:driver/console:driver/framebuffer:driver/input:driver/pci:driver/resource:driver/ide
OBJECTS				=	multiboot.o setup.o krn.o clib.o console.o interrupt.o handler.o debug.o io.o kbd.o pci.o page.o
OBJECTS				+=	timer.o i8259.o task.o resource.o ide.o
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
	qemu -fda OluxOS.img -hda OluxOS.img -boot a -m 256


over: clean OluxOS.krn img emu


