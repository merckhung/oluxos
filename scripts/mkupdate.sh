#!/usr/bin/env bash
# Build a signed OluxOS update bundle from the `make rpi4` boot files.
#   scripts/mkupdate.sh <rpi4-boot-dir> <signing-key.pem> <out.tar> [version]
#
# The bundle is a tar with
#   files/...      the boot slot contents (kernel, initramfs, firmware, ...)
#   manifest       "sha256  path" for every file, plus a VERSION line
#   manifest.sig   Ed25519 signature of manifest (openssl pkeyutl -rawin)
# On the device, `olux-update install` checks the signature against
# /etc/olux/update.pub, then the hashes, then copies only listed files.
set -euo pipefail
BOOT="$1"
KEY="$2"
OUT="$3"
VERSION="${4:-${OLUX_UPDATE_VERSION:-$(git -C "$(dirname "$0")/.." describe --always --dirty 2>/dev/null || echo unknown)}}"
[ -f "$BOOT/kernel8.img" ] || { echo "run 'make rpi4' first" >&2; exit 1; }
[ -f "$KEY" ] || { echo "no signing key $KEY" >&2; exit 1; }
T="$(mktemp -d)"
trap 'rm -rf "$T"' EXIT
mkdir -p "$T/files"
cp -r "$BOOT"/. "$T/files/"
printf '%s\n' "$VERSION" >"$T/files/VERSION"
(cd "$T/files" && find . -type f | sed 's|^\./||' | LC_ALL=C sort | xargs sha256sum) >"$T/manifest"
openssl pkeyutl -sign -inkey "$KEY" -rawin -in "$T/manifest" -out "$T/manifest.sig"
tar -C "$T" -cf "$OUT" manifest manifest.sig files
echo "update bundle: $OUT (version $VERSION, $(wc -l <"$T/manifest") files)" >&2
