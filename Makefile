include make.tmpl


MAJOR_VERSION		=	0
MINOR_VERSION 		=	1
EXTRA_VERSION		=


CROSS_COMPILE       =


ASFLAGS				=	-D__ASM__
CFLAGS              =   -Wall -Wstrict-prototypes -O2 -fomit-frame-pointer -nostdinc -isystem include -DKERNEL_DEBUG
LDFLAGS             =	-cref -M -s -N
MAKEFLAGS			+=	--no-print-directory --no-builtin-rules --no-builtin-variables --quiet


QUIET				?=	QUIET_

QUIET_CMD_AS		?=	AS		$@
	  CMD_AS		?=	$(CC) $(ASFLAGS) $(CFLAGS) -c -o $(<D)/$@ $<

QUIET_CMD_CC		?=	CC		$@
	  CMD_CC		?=	$(CC) $(CFLAGS) -c -o $(<D)/$@ $<

QUIET_CMD_LD		?=	LD		$@
	  CMD_LD		?=	$(LD) $(LDFLAGS) -o $@ $(shell cat $(OBJECTLIST)) > $@.map


ARCH				:=	$(shell uname -m | sed -e s/i.86/ia32/)
VPATH				=	arch/$(ARCH)/boot:arch/$(ARCH)/kernel:arch/$(ARCH)/component:arch/$(ARCH)/lib:arch/$(ARCH)/mm
VPATH				+=	:lib:driver/console:driver/framebuffer:driver/input:driver/pci:driver/resource:driver/ide:fs
BOOTOBJS			=	boot.o info.o pm.o
OBJECTS				=	setup.o krn.o clib.o console.o interrupt.o handler.o debug.o io.o kbd.o pci.o page.o
OBJECTS				+=	timer.o i8259.o task.o resource.o ide.o menu.o fat.o
OBJECTLIST			=	.krnobj.lst
BOOTOBJLIST			=	.bootobj.lst

IMGSIZE				=	1474560
SYSIMG				=	OluxOS.img
KERNELNAME			=	OluxOS.krn
BOOTSECT			=	bootsect



$(KERNELNAME): $(BOOTSECT) $(OBJECTS)
	$(ECHO) '   $($(QUIET)CMD_LD)'
	$(CMD_LD) -T arch/$(ARCH)/kernel.lds


$(BOOTSECT): $(BOOTOBJS)
	$(ECHO) '   $($(QUIET)CMD_LD)'
	$(CMD_LD) -T arch/$(ARCH)/boot/boot.lds
	$(MV) -f $(OBJECTLIST) $(BOOTOBJLIST)


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
	$(RM) -f $(shell cat $(BOOTOBJLIST) 2> /dev/null)
	$(RM) -f $(OBJECTLIST) $(BOOTOBJLIST)
	$(RM) $(BOOTSECT) $(KERNELNAME) *.map *.img *.lst
	$(MAKE) -C image clean


img:
	$(MAKE) -C image all


emu:
	$(DD) if=/dev/zero of=$(SYSIMG) bs=$(IMGSIZE) count=1
	$(DD) if=$(KERNELNAME) of=$(SYSIMG) bs=$(IMGSIZE) count=1 conv=notrunc
	qemu -hda OluxOS.img -m 256


over: clean $(BOOTSECT) $(KERNELNAME) emu


