all:
	gcc -O2 -fno-builtin -nostdinc -DHAVE_CONFIG_H -I. -I. -I..  -Wall -Wmissing-prototypes -Wunused -Wshadow -Wpointer-arith -falign-jumps=1 -falign-loops=1 -falign-functions=1 -Wundef -g -c -o OluxOSKernel.o boot.S
	gcc -g -nostdlib -Wl,-N,-Ttext,7C00 -o OluxOSKernel.exec OluxOSKernel.o
	objcopy -O binary OluxOSKernel.exec OluxOSKernel

as:
	as -o OluxOSKernel.o boot.S
	ld -s -N -Ttext=0x7c00 --oformat=binary -o OluxOSKernel OluxOSKernel.o

clean:
	rm -f *.o *.exec OluxOSKernel
