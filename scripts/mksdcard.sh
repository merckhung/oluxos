#!/usr/bin/env bash
# Build a bootable Raspberry Pi 4 SD card image from the `make rpi4` files.
#   scripts/mksdcard.sh <rpi4-boot-dir> <out.img> [size-MiB]
#
# Layout (MBR):
#   p1  FAT32 "OLUXBOOT", 128 MiB: firmware, config.txt, kernel8.img,
#       initramfs; mounted at /boot by fatfsd
#   p2  FAT32 "OLUXDATA", rest of the image: writable data at /data
# The root filesystem is the initramfs (read-only image, RAM at run time).
# Write it with e.g.  dd if=out/sdcard.img of=/dev/sdX bs=4M conv=fsync
set -euo pipefail
BOOT="$1"
OUT="$2"
SIZE_MB="${3:-256}"
BOOT_MB=128
[ -f "$BOOT/kernel8.img" ] && [ -f "$BOOT/start4.elf" ] || { echo "run 'make rpi4' first" >&2; exit 1; }
[ "$SIZE_MB" -gt $((BOOT_MB + 16)) ] || { echo "image too small" >&2; exit 1; }
T="$(mktemp -d)"
trap 'rm -rf "$T"' EXIT
export MTOOLS_SKIP_CHECK=1
DATA_MB=$((SIZE_MB - BOOT_MB - 1))
truncate -s ${BOOT_MB}M "$T/boot.img"
mkfs.vfat -F 32 -n OLUXBOOT "$T/boot.img" >/dev/null
for f in "$BOOT"/*; do mcopy -s -i "$T/boot.img" "$f" ::/; done
truncate -s ${DATA_MB}M "$T/data.img"
mkfs.vfat -F 32 -n OLUXDATA "$T/data.img" >/dev/null
printf 'OluxOS data partition, mounted read-write at /data.\n' >"$T/README.txt"
mcopy -i "$T/data.img" "$T/README.txt" ::/README.txt
python3 - "$OUT" "$T/boot.img" "$T/data.img" "$SIZE_MB" <<'PY'
import struct, sys
out, boot, data, size_mb = sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4])
b, d = open(boot, 'rb').read(), open(data, 'rb').read()
start1 = 2048
start2 = start1 + len(b) // 512
mbr = bytearray(512)
struct.pack_into('<I', mbr, 440, 0x4f4c5558)  # disk signature "OLUX"
def entry(i, typ, start, n):
    struct.pack_into('<B3sB3sII', mbr, 446 + 16 * i, 0x80 if i == 0 else 0, b'\x00\x02\x00', typ,
                     b'\xff\xff\xff', start, n)
entry(0, 0x0c, start1, len(b) // 512)
entry(1, 0x0c, start2, len(d) // 512)
mbr[510:512] = b'\x55\xaa'
with open(out, 'wb') as f:
    f.write(mbr)
    f.seek(start1 * 512); f.write(b)
    f.seek(start2 * 512); f.write(d)
    f.truncate(size_mb << 20)
PY
echo "SD card image: $OUT (${SIZE_MB} MiB)" >&2
