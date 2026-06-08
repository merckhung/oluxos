#!/bin/bash
KERNEL_PATH=$1

if [ -z "$KERNEL_PATH" ]; then
    echo "Usage: $0 <path_to_kernel_elf>"
    exit 1
fi

echo "Booting OluxOS ARM64 at $KERNEL_PATH with QEMU..."
# -nographic redirects serial to console
# -bios none because we boot ELF directly
qemu-system-aarch64 \
    -M virt \
    -cpu max \
    -m 1024M \
    -kernel "$KERNEL_PATH" -nographic \
    -device loader,file=fat.img,addr=0x48000000,force-raw=on
