#!/usr/bin/env bash
# Boot OluxOS in QEMU (AArch64 virt).
#   scripts/run-qemu.sh [out-dir] [extra qemu args...]
# Environment: SMP (default 4), MEM (default 1G), GIC (2|3), CPU (cortex-a72|max)
set -euo pipefail
OUT="${1:-out}"
shift || true
DISK=()
if [ -f "$OUT/disk.img" ]; then
  DISK=(-drive "if=none,file=$OUT/disk.img,format=raw,id=hd0" -device virtio-blk-device,drive=hd0)
fi
exec qemu-system-aarch64 -M "virt,gic-version=${GIC:-2}" -cpu "${CPU:-cortex-a72}" -smp "${SMP:-4}" \
  -m "${MEM:-1G}" -kernel "$OUT/Image" -initrd "$OUT/initramfs.cpio" -append "console=ttyAMA0 ${APPEND:-}" \
  -nographic -no-reboot "${DISK[@]}" \
  -netdev user,id=n0,hostfwd=tcp::5555-:23,hostfwd=tcp::8080-:80 -device virtio-net-device,netdev=n0 \
  -device virtio-rng-device "$@"
