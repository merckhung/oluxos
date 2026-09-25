#!/usr/bin/env bash
###############################################################################
# Build a complete AArch64 cross toolchain for OluxOS userspace from source:
#
#   binutils 2.42 + GCC 13.3.0 (C only) + musl 1.2.5
#   target triple: aarch64-linux-musl, static-only
#   installed to:  toolchains/cross/install   (override with PREFIX=...)
#
# Use this on hosts without a packaged AArch64 cross compiler. After it
# finishes, toolchains/userspace/build.sh picks the toolchain up automatically.
#
# Sources are downloaded once into toolchains/cross/sources (override with
# SOURCES=...). Every tarball is verified against a pinned SHA-256. For fully
# offline builds, pre-populate SOURCES with the tarballs listed below.
#
# Host requirements: a C/C++ compiler, make, bison, flex, texinfo, xz, bzip2,
# curl (or wget) and ~6 GB of disk. Takes 20-60 minutes.
###############################################################################
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
TARGET=aarch64-linux-musl
PREFIX="${PREFIX:-$SCRIPT_DIR/install}"
SOURCES="${SOURCES:-$SCRIPT_DIR/sources}"
BUILD="${BUILD:-$SCRIPT_DIR/build}"
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 4)}"
SYSROOT="$PREFIX/$TARGET"

BINUTILS_V=2.42
GCC_V=13.3.0
GMP_V=6.3.0
MPFR_V=4.2.1
MPC_V=1.3.1
MUSL_V=1.2.5

log() { printf '[cross] %s\n' "$*" >&2; }
die() { printf '[cross] ERROR: %s\n' "$*" >&2; exit 1; }

download() { # url dest
  if command -v curl >/dev/null 2>&1; then curl -fsSL --retry 3 -o "$2" "$1"
  else wget -q -O "$2" "$1"; fi
}

# fetch <file> "<sha256> [<sha256>...]" <url>...
fetch() {
  local file="$1" sums="$2"; shift 2
  local dest="$SOURCES/$file" got url
  mkdir -p "$SOURCES"
  if [ -f "$dest" ]; then
    got="$(sha256sum "$dest" | cut -d' ' -f1)"
    case " $sums " in *" $got "*) return 0 ;; esac
    log "$file has an unexpected checksum; re-downloading"
    rm -f "$dest"
  fi
  for url in "$@"; do
    log "fetching $url"
    if download "$url" "$dest.part" 2>/dev/null; then
      got="$(sha256sum "$dest.part" | cut -d' ' -f1)"
      case " $sums " in
        *" $got "*) mv "$dest.part" "$dest"; return 0 ;;
        *) log "  checksum mismatch ($got), trying next mirror" ;;
      esac
    fi
    rm -f "$dest.part"
  done
  return 1
}

GNU_MIRRORS=(https://ftpmirror.gnu.org/gnu https://ftp.gnu.org/gnu https://mirrors.kernel.org/gnu)
UBUNTU=http://archive.ubuntu.com/ubuntu/pool/main

gnu_urls() { # subdir file
  local m; for m in "${GNU_MIRRORS[@]}"; do echo "$m/$1/$2"; done
}

fetch_all() {
  # shellcheck disable=SC2046
  fetch binutils-$BINUTILS_V.tar.xz f6e4d41fd5fc778b06b7891457b3620da5ecea1006c6a4a41ae998109f85a800 \
    $(gnu_urls binutils binutils-$BINUTILS_V.tar.xz) "$UBUNTU/b/binutils/binutils_$BINUTILS_V.orig.tar.xz" \
    || die "cannot fetch binutils"
  # GMP: upstream tarball, or Debian's DFSG repack (documentation removed).
  # shellcheck disable=SC2046
  fetch gmp-$GMP_V.tar.xz "a3c2b80201b89e68616f4ad30bc66aee4927c3ce50e33929ca819d5c43538898 bd2966e6d277f79328e894a5a9f3ba3fbf2ed2be81def5f48623e30c23fb1572" \
    $(gnu_urls gmp gmp-$GMP_V.tar.xz) "$UBUNTU/g/gmp/gmp_$GMP_V+dfsg.orig.tar.xz" \
    || die "cannot fetch gmp"
  # shellcheck disable=SC2046
  fetch mpfr-$MPFR_V.tar.xz 277807353a6726978996945af13e52829e3abd7a9a5b7fb2793894e18f1fcbb2 \
    $(gnu_urls mpfr mpfr-$MPFR_V.tar.xz) "$UBUNTU/m/mpfr4/mpfr4_$MPFR_V.orig.tar.xz" \
    || die "cannot fetch mpfr"
  # shellcheck disable=SC2046
  fetch mpc-$MPC_V.tar.gz ab642492f5cf882b74aa0cb730cd410a81edcdbec895183ce930e706c1c759b8 \
    $(gnu_urls mpc mpc-$MPC_V.tar.gz) "$UBUNTU/m/mpclib3/mpclib3_$MPC_V.orig.tar.gz" \
    || die "cannot fetch mpc"
  # shellcheck disable=SC2046
  if ! fetch gcc-$GCC_V.tar.xz 0845e9621c9543a13f484e94584a49ffc0129970e9914624235fc1d061a0c083 \
      $(gnu_urls gcc/gcc-$GCC_V gcc-$GCC_V.tar.xz); then
    # Fallback: the official GCC git mirror at the release tag.
    if [ ! -d "$SOURCES/gcc-$GCC_V.git" ]; then
      log "tarball unavailable; cloning GCC releases/gcc-$GCC_V from git"
      git clone -q --depth 1 --branch "releases/gcc-$GCC_V" \
        https://github.com/gcc-mirror/gcc "$SOURCES/gcc-$GCC_V.git" || die "cannot fetch gcc"
    fi
  fi
}

unpack() { # tarball-glob dir
  local dir="$2"
  [ -d "$BUILD/$dir" ] && return 0
  log "unpacking $1"
  mkdir -p "$BUILD/.x"
  tar -xf "$SOURCES/$1" -C "$BUILD/.x"
  mv "$BUILD/.x/"* "$BUILD/$dir"
}

main() {
  command -v make >/dev/null || die "make is required"
  mkdir -p "$BUILD" "$PREFIX"
  fetch_all
  export PATH="$PREFIX/bin:$PATH"

  # 1. binutils
  if [ ! -x "$PREFIX/bin/$TARGET-ld" ]; then
    unpack binutils-$BINUTILS_V.tar.xz binutils-src
    mkdir -p "$BUILD/binutils" && cd "$BUILD/binutils"
    "$BUILD/binutils-src/configure" --target=$TARGET --prefix="$PREFIX" \
      --with-sysroot="$SYSROOT" --disable-nls --disable-werror --disable-multilib \
      --disable-gdb --disable-gprofng >/dev/null
    make -j"$JOBS" >/dev/null && make install >/dev/null
  fi

  # 2. musl headers (needed to build libgcc)
  if [ ! -f "$SYSROOT/include/stdio.h" ]; then
    rm -rf "$BUILD/musl-src"
    tar -xzf "$REPO_ROOT/third_party/musl/musl-$MUSL_V.tar.gz" -C "$BUILD"
    mv "$BUILD/musl-$MUSL_V" "$BUILD/musl-src"
    make -C "$BUILD/musl-src" ARCH=aarch64 prefix= DESTDIR="$SYSROOT" install-headers >/dev/null
  fi

  # 3. gcc (C only, static libgcc)
  if [ ! -x "$PREFIX/bin/$TARGET-gcc" ]; then
    if [ -f "$SOURCES/gcc-$GCC_V.tar.xz" ]; then unpack gcc-$GCC_V.tar.xz gcc-src
    elif [ ! -d "$BUILD/gcc-src" ]; then cp -a "$SOURCES/gcc-$GCC_V.git" "$BUILD/gcc-src"; fi
    unpack gmp-$GMP_V.tar.xz gmp-src
    # Debian's DFSG repack of GMP drops doc/; configure still expects it.
    if [ ! -f "$BUILD/gmp-src/doc/Makefile.in" ]; then
      mkdir -p "$BUILD/gmp-src/doc"
      printf 'all:\ninstall:\ncheck:\nclean:\ndistclean:\n\t@rm -f Makefile\n.PHONY: all install check clean distclean\n' \
        >"$BUILD/gmp-src/doc/Makefile.in"
    fi
    unpack mpfr-$MPFR_V.tar.xz mpfr-src
    unpack mpc-$MPC_V.tar.gz mpc-src
    ln -sfn "$BUILD/gmp-src" "$BUILD/gcc-src/gmp"
    ln -sfn "$BUILD/mpfr-src" "$BUILD/gcc-src/mpfr"
    ln -sfn "$BUILD/mpc-src" "$BUILD/gcc-src/mpc"
    mkdir -p "$BUILD/gcc" && cd "$BUILD/gcc"
    "$BUILD/gcc-src/configure" --target=$TARGET --prefix="$PREFIX" \
      --with-sysroot="$SYSROOT" --enable-languages=c --disable-shared \
      --disable-multilib --disable-nls --disable-libssp --disable-libsanitizer \
      --disable-libquadmath --disable-libgomp --disable-libatomic --disable-libitm \
      --disable-libvtv --disable-libstdcxx --enable-default-pie=no \
      --with-arch=armv8-a >/dev/null
    make -j"$JOBS" all-gcc all-target-libgcc >/dev/null
    make install-gcc install-target-libgcc >/dev/null
  fi

  # 4. musl libc proper
  if [ ! -f "$SYSROOT/lib/libc.a" ]; then
    cd "$BUILD/musl-src"
    CC="$TARGET-gcc" ./configure --target=$TARGET --prefix= --disable-shared >/dev/null
    make -j"$JOBS" >/dev/null
    make DESTDIR="$SYSROOT" install >/dev/null
  fi

  printf 'int main(void){return 0;}\n' >"$BUILD/t.c"
  "$TARGET-gcc" -static -o "$BUILD/t" "$BUILD/t.c" || die "toolchain sanity check failed"
  log "toolchain ready: $PREFIX/bin/$TARGET-gcc"
}

main "$@"
