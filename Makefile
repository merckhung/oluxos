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


CROSS_COMPILE       =


ASFLAGS				=
CFLAGS              =   -Iinclude -Wall -Wstrict-prototypes -O2 -fomit-frame-pointer
LDFLAGS             =	-cref -M -s -N -T arch/$(ARCH)/multiboot/multiboot.lds
MAKEFLAGS			+=	--no-print-directory --no-builtin-rules --no-builtin-variables --quiet


QUITE				?=	QUITE_

QUITE_CMD_CC		?=	CC		$@
	  CMD_CC		?=	$(CC) $(CFLAGS) -c -o $@ $<

QUITE_CMD_LD		?=	LD		$@
	  CMD_LD		?=	$(LD) $(LDFLAGS) -o $@ $+ > $@.map


ARCH				:=	$(shell uname -m | sed -e s/i.86/ia32/)
VPATH				=	arch/$(ARCH)/kernel arch/$(ARCH)/lib arch/$(ARCH)/mm arch/$(ARCH)/multiboot lib
OBJECTS				=	ia32_krn.o interrupt.o int_handler.o console.o debug.o io.o kbd.o pci.o page.o multiboot.o routine.o



OluxOS.krn: $(OBJECTS)
	@echo '   $($(QUITE)CMD_LD)'
	$(CMD_LD)


%.o: %.c
	@echo '   $($(QUITE)CMD_CC)'
	$(CMD_CC)


clean:
	$(RM) *.krn *.map *.img *.o


img:
	make -C image all


emu:
	sudo modprobe kqemu
	qemu -fda OluxOS.img -m 256


over: clean all img emu


