CROSS_COMPILE		=	arm-none-eabi-
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
DD					=	dd


ARCH                =   armv5te
CPU					=	xscale
CFLAGS          	=   -Wall -Wstrict-prototypes -O2 -fomit-frame-pointer -nostdinc -isystem include -DKERNEL_DEBUG -mtune=$(CPU) -march=$(ARCH)
LDFLAGS         	=   -cref -M -s -N


OBJS				=	boot.o uart_pxa.o uart_msm.o routine.o krn.o


QEMU				=	qemu-system-arm
MEMSIZE				=	256
IMGSIZE				=	67108864
PLATFORM			=	mainstone


all: clean boot.bin


boot.bin: $(OBJS)
	$(LD) $(LDFLAGS) -o boot.exec $(OBJS) > boot.map -T boot.lds
	$(OBJCOPY) -O binary boot.exec $@



%.o: %.S
	$(CC) $(ASFLAGS) $(CFLAGS) -c -o $@ $<


%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<


dis:
	$(OBJDUMP) -D -b binary -m $(ARCH) boot.bin


emu:
	$(QEMU) -M $(PLATFORM) -m $(MEMSIZE) -kernel ./boot.bin -nographic


clean:
	rm -f *.o *.exec *.bin *.map


dump:
	$(OBJDUMP) -D -m $(ARCH) -b binary boot.bin


over: clean all emu


