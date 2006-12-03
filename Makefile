all:
	make -C arch/ia32/multiboot
	make -C arch/ia32/kernel
	make -C arch/ia32/lib
	make -C arch/ia32/mm
	ld -cref -M -s -N -T arch/ia32/multiboot/multiboot.lds -o OluxOS.krn arch/ia32/multiboot/multiboot.o arch/ia32/kernel/ia32_krn.o arch/ia32/lib/console.o arch/ia32/lib/debug.o arch/ia32/lib/routine.o arch/ia32/mm/page.o > OluxOS.map


clean:
	make -C arch/ia32/multiboot clean
	make -C arch/ia32/kernel clean
	make -C arch/ia32/lib clean
	make -C arch/ia32/mm clean
	make -C image clean
	rm -f OluxOS.krn *.map *.img


img:
	make -C image all


emu:
	sudo modprobe kqemu
	qemu -fda OluxOS.img -m 256


dump:
	objdump -m i386 -b binary -D OluxOS.krn | less


nasm:
	ndisasm OluxOS.krn | less


over: clean all img emu
