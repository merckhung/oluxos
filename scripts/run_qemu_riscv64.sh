#!/bin/bash
KERNEL_ELF=$1

if [ -z "$KERNEL_ELF" ]; then
    echo "Usage: $0 <path_to_kernel_elf>"
    exit 1
fi

echo "Booting OluxOS RISC-V 64-bit at $KERNEL_ELF with QEMU..."
shift
qemu-system-riscv64 -M virt -cpu rv64 -smp 4 -m 1024M -kernel "$KERNEL_ELF" -nographic \
    -device loader,file=fat.img,addr=0x88000000,force-raw=on "$@"
