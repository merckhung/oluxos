#!/usr/bin/env bash
# Build a bootable Raspberry Pi 4 SD card image from the `make rpi4` files.
#   scripts/mksdcard.sh <rpi4-boot-dir> <out.img> [size-MiB]
#
# A/B layout (MBR), using the firmware's autoboot.txt / tryboot mechanism:
#   p1  FAT16 "OLUXAUTO", 16 MiB: autoboot.txt only; says which slot boots
#       normally and which one a "tryboot" reboot tries once
#   p2  FAT32 "OLUXBOOTA", 128 MiB: slot A (firmware, config.txt,
#       kernel8.img, initramfs, cmdline.txt with olux.slot=a)
#   p3  FAT32 "OLUXBOOTB", 128 MiB: slot B (same, olux.slot=b)
#   p4  FAT32 "OLUXDATA", the rest: writable data at /data
# The running slot is mounted at /boot. `olux-update` installs a signed
# update into the other slot, trial-boots it with tryboot, and makes it the
# default once it came up (a failed trial falls back to the old slot).
# Write it with e.g.  dd if=out/sdcard.img of=/dev/sdX bs=4M conv=fsync
set -euo pipefail
BOOT="$1"
OUT="$2"
SIZE_MB="${3:-512}"
AUTO_MB=16
SLOT_MB=128
[ -f "$BOOT/kernel8.img" ] && [ -f "$BOOT/start4.elf" ] || { echo "run 'make rpi4' first" >&2; exit 1; }
[ "$SIZE_MB" -gt $((AUTO_MB + 2 * SLOT_MB + 16)) ] || { echo "image too small" >&2; exit 1; }
T="$(mktemp -d)"
trap 'rm -rf "$T"' EXIT
export MTOOLS_SKIP_CHECK=1

truncate -s ${AUTO_MB}M "$T/auto.img"
mkfs.vfat -F 16 -n OLUXAUTO "$T/auto.img" >/dev/null
cat >"$T/autoboot.txt" <<'AB'
[all]
tryboot_a_b=1
boot_partition=2
[tryboot]
boot_partition=3
AB
mcopy -i "$T/auto.img" "$T/autoboot.txt" ::/autoboot.txt

for slot in a b; do
  truncate -s ${SLOT_MB}M "$T/slot-$slot.img"
  mkfs.vfat -F 32 -n "OLUXBOOT$(echo $slot | tr ab AB)" "$T/slot-$slot.img" >/dev/null
  for f in "$BOOT"/*; do
    [ "$(basename "$f")" = cmdline.txt ] && continue
    mcopy -s -i "$T/slot-$slot.img" "$f" ::/
  done
  printf '%s olux.slot=%s\n' "$(sed 's/ *olux\.slot=[ab]//' "$BOOT/cmdline.txt" | tr -d '\n')" "$slot" >"$T/cmdline.txt"
  mcopy -i "$T/slot-$slot.img" "$T/cmdline.txt" ::/cmdline.txt
done

DATA_MB=$((SIZE_MB - AUTO_MB - 2 * SLOT_MB - 1))
truncate -s ${DATA_MB}M "$T/data.img"
mkfs.vfat -F 32 -n OLUXDATA "$T/data.img" >/dev/null
printf 'OluxOS data partition, mounted read-write at /data.\n' >"$T/README.txt"
mcopy -i "$T/data.img" "$T/README.txt" ::/README.txt

python3 - "$OUT" "$SIZE_MB" "$T/auto.img" "$T/slot-a.img" "$T/slot-b.img" "$T/data.img" <<'PY'
import os, struct, sys
out, size_mb, parts = sys.argv[1], int(sys.argv[2]), sys.argv[3:]
mbr = bytearray(512)
struct.pack_into('<I', mbr, 440, 0x4f4c5558)  # disk signature "OLUX"
start = 2048
layout = []
for i, p in enumerate(parts):
    n = os.path.getsize(p) // 512
    typ = 0x0e if i == 0 else 0x0c  # FAT16 LBA, FAT32 LBA
    struct.pack_into('<B3sB3sII', mbr, 446 + 16 * i, 0x80 if i == 0 else 0, b'\x00\x02\x00', typ,
                     b'\xff\xff\xff', start, n)
    layout.append((p, start))
    start += n
mbr[510:512] = b'\x55\xaa'
with open(out, 'wb') as f:
    f.write(mbr)
    for p, s in layout:
        f.seek(s * 512)
        with open(p, 'rb') as src:
            f.write(src.read())
    f.truncate(size_mb << 20)
PY
echo "SD card image: $OUT (${SIZE_MB} MiB, A/B slots)" >&2
