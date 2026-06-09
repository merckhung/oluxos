#!/bin/bash
KERNEL_ELF=$1

if [ -z "$KERNEL_ELF" ]; then
    echo "Usage: $0 <path_to_kernel_elf>"
    exit 1
fi

echo "Booting OluxOS RISC-V 32-bit at $KERNEL_ELF with QEMU..."
shift
qemu-system-riscv32 -M virt -m 128M -kernel "$KERNEL_ELF" -nographic "$@"
