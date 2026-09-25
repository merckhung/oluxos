#!/bin/bash
KERNEL_PATH=$1

if [ -z "$KERNEL_PATH" ]; then
    echo "Usage: $0 <path_to_kernel_elf>"
    exit 1
fi

echo "Booting OluxOS on Raspberry Pi 4B (QEMU) at $KERNEL_PATH..."
GRAPHIC_OPT="-nographic"
SERIAL_OPT=""

if [ "$GUI" = "1" ]; then
    GRAPHIC_OPT=""
    if [ "$PTY" = "1" ]; then
        SERIAL_OPT="-serial pty"
    else
        SERIAL_OPT="-serial stdio"
    fi
else
    if [ "$PTY" = "1" ]; then
        SERIAL_OPT="-serial pty"
    fi
fi

qemu-system-aarch64 -M raspi4b -cpu cortex-a72 -m 2G -kernel "$KERNEL_PATH" \
    $GRAPHIC_OPT \
    $SERIAL_OPT \
    -device loader,file="${2:-bazel-bin/rootfs.img}",addr=0x48000000,force-raw=on
