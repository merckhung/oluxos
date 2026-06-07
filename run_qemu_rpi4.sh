#!/bin/bash
KERNEL_PATH=$1

if [ -z "$KERNEL_PATH" ]; then
    echo "Usage: $0 <path_to_kernel_elf>"
    exit 1
fi

echo "Booting OluxOS on Raspberry Pi 4B (QEMU) at $KERNEL_PATH..."
SERIAL_OPT=""
if [ "$PTY" = "1" ]; then
    SERIAL_OPT="-serial pty"
fi

qemu-system-aarch64 -M raspi4b -cpu cortex-a72 -m 2G -kernel "$KERNEL_PATH" -nographic \
    $SERIAL_OPT \
    -device loader,file=fat.img,addr=0x48000000,force-raw=on
