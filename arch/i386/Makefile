AS=as
CC=gcc
LD=ld
OBJDUMP=objdump
OBJCOPY=objcopy

TEXTADDR=0x7c00

all: clean boot.bin floppy.img


boot.bin: boot.o
	$(LD) -s -N -T boot.lds --oformat=binary -o $@ $<


boot.o: boot.S
	$(AS) -a=boot.lst -o $@ $<
	

floppy.img:
	dd if=boot.bin of=OluxOS_floppy.img conv=sync ibs=1474560

	
dump:
	#$(OBJDUMP) -m i386 -b binary -D boot.bin
	#echo ""
	ndisasm boot.bin


clean:
	rm -f *.o *.bin *.img *.lst
