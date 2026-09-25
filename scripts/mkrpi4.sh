#!/usr/bin/env bash
###############################################################################
# Assemble a Raspberry Pi 4B boot partition for OluxOS.
#
#   scripts/mkrpi4.sh <build-dir> <output-dir> [--no-firmware]
#
# <build-dir> must contain Image and initramfs.cpio (from `make`). The output
# directory receives:
#   kernel8.img      OluxOS kernel (arm64 Image; loaded by the firmware)
#   initramfs.cpio   root filesystem
#   config.txt       firmware configuration
#   cmdline.txt      kernel command line (exposed via /chosen/bootargs)
#   VERSION          image version (git describe)
#   start4.elf, fixup4.dat, bcm2711-rpi-4-b.dtb, overlays/*.dtbo
#                    Raspberry Pi firmware (fetched from the pinned release,
#                    SHA-256 verified; redistributable per its own licence)
# Copy the directory's contents onto a FAT32 partition of an SD card.
###############################################################################
set -euo pipefail
BUILD="${1:?build dir}"
OUTDIR="${2:?output dir}"
FIRMWARE=1
[ "${3:-}" = "--no-firmware" ] && FIRMWARE=0

FW_TAG=1.20250430
FW_URL="https://raw.githubusercontent.com/raspberrypi/firmware/${FW_TAG}/boot"
CACHE="${OLUX_FW_CACHE:-$(dirname "$0")/../toolchains/rpi-firmware/$FW_TAG}"
declare -A FW_SHA=(
  [start4.elf]=2cc344f6171797317ae0b6197191803b45e2f3cda091f0da31c0a3f0bfc47593
  [fixup4.dat]=ca84b2fae239fc9041ac1bdc8648c1e33b9c6e67c4bd073b651b96dc2726c553
  [bcm2711-rpi-4-b.dtb]=4bc9dd6182025add23a750b18ff748e46d4e939434530e8057c6bfa1ce6f1c16
  [overlays/disable-bt.dtbo]=ea69d22dedc607fee75eec57d8a4cc0f0eab93cd75393e61a64c49fbac912d02
)

mkdir -p "$OUTDIR"
cp "$BUILD/Image" "$OUTDIR/kernel8.img"
cp "$BUILD/initramfs.cpio" "$OUTDIR/initramfs.cpio"

cat >"$OUTDIR/config.txt" <<'EOF'
# OluxOS on Raspberry Pi 4B
arm_64bit=1
kernel=kernel8.img
initramfs initramfs.cpio followkernel
# PL011 (ttyAMA0) on GPIO 14/15 at 115200 8N1; Bluetooth moved off it
enable_uart=1
dtoverlay=disable-bt
uart_2ndstage=1
# The kernel supports memory above 4 GiB (8 GB boards)
total_mem=8192
# Leave the display at firmware defaults; OluxOS uses the mailbox framebuffer
disable_overscan=1
gpu_mem=64
# Hardware watchdog is driven by the kernel (bcm2835-pm-wdt)
dtparam=watchdog=on
# I2C1 on GPIO 2/3 (/dev/i2c-1) and SPI0 on GPIO 7-11 (/dev/spidev0.*)
dtparam=i2c_arm=on
dtparam=spi=on
EOF

echo "console=ttyAMA0,115200 loglevel=6" >"$OUTDIR/cmdline.txt"
# image version, shown by `olux-update status`
git -C "$(dirname "$0")/.." describe --always --dirty 2>/dev/null >"$OUTDIR/VERSION" || echo unknown >"$OUTDIR/VERSION"

if [ "$FIRMWARE" = 1 ]; then
  mkdir -p "$CACHE/overlays" "$OUTDIR/overlays"
  for f in "${!FW_SHA[@]}"; do
    if [ ! -f "$CACHE/$f" ] || [ "$(sha256sum "$CACHE/$f" | cut -d' ' -f1)" != "${FW_SHA[$f]}" ]; then
      echo "fetching $f" >&2
      curl -fsSL --retry 3 -o "$CACHE/$f.part" "$FW_URL/$f"
      got="$(sha256sum "$CACHE/$f.part" | cut -d' ' -f1)"
      if [ "$got" != "${FW_SHA[$f]}" ]; then
        echo "checksum mismatch for $f: $got" >&2
        exit 1
      fi
      mv "$CACHE/$f.part" "$CACHE/$f"
    fi
    cp "$CACHE/$f" "$OUTDIR/$f"
  done
fi
echo "Raspberry Pi 4 boot files written to $OUTDIR" >&2
