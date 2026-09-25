#!/usr/bin/env bash
# Build the OluxOS root filesystem image (FAT32, 32 MiB).
# Usage: mkrootfs.sh <out.img> <busybox> <loader.elf> <tposix.elf>
set -euo pipefail
out="$1" busybox="$2" loader="$3" tposix="$4"
rm -f "$out"
truncate -s 32M "$out"
mkfs.vfat -F 32 -s 1 -n OLUXOS --invariant -i 0x01105eed "$out" >/dev/null
export MTOOLS_SKIP_CHECK=1
mcopy -i "$out" "$busybox" ::/sh
mcopy -i "$out" "$busybox" ::/busybox
mcopy -i "$out" "$loader" ::/loader.elf
mcopy -i "$out" "$tposix" ::/tposix.elf
