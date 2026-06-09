#!/bin/bash
KERNEL_ELF=$1

if [ -z "$KERNEL_ELF" ]; then
    echo "Usage: $0 <path_to_kernel_elf>"
    exit 1
fi

echo "Booting OluxOS ARM32 at $KERNEL_ELF with QEMU..."
shift
qemu-system-arm -M virt -cpu cortex-a15 -m 128 -kernel "$KERNEL_ELF" -nographic "$@"
