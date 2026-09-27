#!/usr/bin/env bash
# Install QEMU 9.2 (needed for the raspi4b machine) next to the distro QEMU,
# from the Ubuntu archive, and create a `qemu9-aarch64` wrapper.
set -euo pipefail
V=9.2.1+ds-1ubuntu5.2
BASE=http://archive.ubuntu.com/ubuntu/pool/main/q/qemu
DEST=${QEMU9_DIR:-/opt/qemu9}
mkdir -p "$DEST/debs"
for p in qemu-system-arm_${V}_amd64.deb qemu-system-common_${V}_amd64.deb qemu-system-data_${V}_all.deb; do
  curl -fsSL --retry 3 -o "$DEST/debs/$p" "$BASE/$p"
  dpkg -x "$DEST/debs/$p" "$DEST/root"
done
cat > /usr/local/bin/qemu9-aarch64 <<WRAP
#!/bin/sh
# Private mount namespace so QEMU 9.2 loads its own modules, not the distro's.
exec unshare -m sh -c 'mount --bind $DEST/root/usr/lib/x86_64-linux-gnu/qemu /usr/lib/x86_64-linux-gnu/qemu && exec $DEST/root/usr/bin/qemu-system-aarch64 -L $DEST/root/usr/share/qemu "\$@"' qemu9 "\$@"
WRAP
chmod +x /usr/local/bin/qemu9-aarch64
qemu9-aarch64 --version | head -1
