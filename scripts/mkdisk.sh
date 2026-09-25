#!/usr/bin/env bash
# Build a test disk image: MBR with p1 = FAT32 (64 MiB), p2 = ext4 (32 MiB).
#   scripts/mkdisk.sh <out.img> [busybox]
set -euo pipefail
OUT="$1"
BB="${2:-toolchains/userspace/out/bin/busybox}"
T="$(mktemp -d)"
trap 'rm -rf "$T"' EXIT
FAT_MB=64
EXT_MB=32
truncate -s ${FAT_MB}M "$T/fat.img"
mkfs.vfat -F 32 -n OLUXDATA "$T/fat.img" >/dev/null
export MTOOLS_SKIP_CHECK=1
echo "Hello from FAT32" >"$T/hello.txt"
mmd -i "$T/fat.img" ::/docs ::/docs/nested
mcopy -i "$T/fat.img" "$T/hello.txt" ::/hello.txt
mcopy -i "$T/fat.img" "$T/hello.txt" "::/docs/nested/A Long File Name.txt"
[ -f "$BB" ] && mcopy -i "$T/fat.img" "$BB" ::/busybox
mkdir -p "$T/ext/etc" "$T/ext/data"
echo "Hello from ext4" >"$T/ext/hello.txt"
echo "config=1" >"$T/ext/etc/app.conf"
ln -s hello.txt "$T/ext/link-to-hello"
dd if=/dev/urandom of="$T/ext/data/blob.bin" bs=1k count=300 2>/dev/null
( cd "$T/ext" && sha256sum data/blob.bin >"$T/ext/data/blob.sha256" )
truncate -s ${EXT_MB}M "$T/ext.img"
mke2fs -q -t ext4 -L oluxext -d "$T/ext" "$T/ext.img"
python3 - "$OUT" "$T/fat.img" "$T/ext.img" <<'PY'
import struct, sys
out, fat, ext = sys.argv[1:]
fat_d, ext_d = open(fat, 'rb').read(), open(ext, 'rb').read()
start1 = 2048
start2 = start1 + len(fat_d) // 512
total = start2 + len(ext_d) // 512 + 2048
mbr = bytearray(512)
def entry(i, typ, start, n):
    struct.pack_into('<B3sB3sII', mbr, 446 + 16 * i, 0, b'\x00\x02\x00', typ, b'\xff\xff\xff', start, n)
entry(0, 0x0c, start1, len(fat_d) // 512)   # FAT32 LBA
entry(1, 0x83, start2, len(ext_d) // 512)   # Linux
mbr[510:512] = b'\x55\xaa'
with open(out, 'wb') as f:
    f.write(mbr)
    f.seek(start1 * 512); f.write(fat_d)
    f.seek(start2 * 512); f.write(ext_d)
    f.truncate(total * 512)
PY
echo "disk image: $OUT" >&2
