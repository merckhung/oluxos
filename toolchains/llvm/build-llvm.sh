#!/bin/bash
###############################################################################
# OluxOS Toolchain — Full Build Script
#
# Builds:
#   1. LLVM/Clang 22.1.7 with OluxOS target patch
#      → produces oluxos-clang (native aarch64-unknown-oluxos support)
#
# BusyBox and musl are built by toolchains/userspace/build.sh.
#
# Usage:
#   ./build-llvm.sh
#
# Prerequisites (Ubuntu/Debian):
#   sudo apt install build-essential cmake ninja-build python3 wget bzip2
#   sudo apt install gcc-aarch64-linux-gnu g++-aarch64-linux-gnu binutils-aarch64-linux-gnu
#   sudo apt install lld clang
###############################################################################
set -euo pipefail

# ── Configuration ─────────────────────────────────────────────────────────────
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
WORK_DIR="${WORK_DIR:-$SCRIPT_DIR/work}"
OUT_DIR="${OUT_DIR:-$SCRIPT_DIR}"

LLVM_VERSION="22.1.7"
LLVM_SRC_URL="https://github.com/llvm/llvm-project/releases/download/llvmorg-${LLVM_VERSION}/llvm-project-${LLVM_VERSION}.src.tar.xz"
LLVM_PATCH="$SCRIPT_DIR/llvm-project-oluxos.patch"


# Number of parallel jobs (auto-detect)
JOBS="$(nproc)"

# ── Color helpers ─────────────────────────────────────────────────────────────
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; CYAN='\033[0;36m'; NC='\033[0m'
info()  { echo -e "${CYAN}[INFO]${NC}  $*"; }
ok()    { echo -e "${GREEN}[OK]${NC}    $*"; }
warn()  { echo -e "${YELLOW}[WARN]${NC}  $*"; }
fail()  { echo -e "${RED}[FAIL]${NC}  $*"; exit 1; }

# ── Dependency checks ────────────────────────────────────────────────────────
check_deps() {
    local missing=()
    for cmd in cmake ninja python3 wget tar make; do
        command -v "$cmd" &>/dev/null || missing+=("$cmd")
    done
    if [ ${#missing[@]} -gt 0 ]; then
        fail "Missing tools: ${missing[*]}. Install with: sudo apt install ${missing[*]}"
    fi
    ok "Build dependencies OK"
}

# ── LLVM/Clang build ─────────────────────────────────────────────────────────
build_llvm() {
    info "=== Building LLVM/Clang ${LLVM_VERSION} with OluxOS patch ==="

    local LLVM_SRC="$WORK_DIR/llvm-project-${LLVM_VERSION}.src"
    local LLVM_BUILD="$WORK_DIR/llvm-build"
    local LLVM_INSTALL="$OUT_DIR/llvm-install"

    # Step 1: Download source
    if [ -d "$LLVM_SRC" ]; then
        info "LLVM source already present: $LLVM_SRC"
    else
        info "Downloading LLVM ${LLVM_VERSION} source..."
        mkdir -p "$WORK_DIR"
        local TAR_FILE="$WORK_DIR/llvm-project-${LLVM_VERSION}.src.tar.xz"
        wget -q --show-progress -O "$TAR_FILE" "$LLVM_SRC_URL" || fail "Failed to download LLVM source"
        info "Extracting..."
        tar xf "$TAR_FILE" -C "$WORK_DIR"
        rm -f "$TAR_FILE"
        ok "LLVM source extracted"
    fi

    # Step 2: Apply OluxOS patch
    if [ -f "$LLVM_SRC/.oluxos-patched" ]; then
        info "OluxOS patch already applied"
    else
        info "Applying OluxOS patch..."
        # The patch paths are prefixed with llvm-project-XX.src/ — strip 1 level
        (cd "$LLVM_SRC" && patch -p1 < "$LLVM_PATCH") || fail "Failed to apply OluxOS patch"



        touch "$LLVM_SRC/.oluxos-patched"
        ok "OluxOS patch applied"
    fi

    # Step 3: Configure
    if [ -f "$LLVM_BUILD/CMakeCache.txt" ]; then
        info "LLVM already configured"
    else
        info "Configuring LLVM..."
        mkdir -p "$LLVM_BUILD"
        cmake -S "$LLVM_SRC/llvm" \
              -B "$LLVM_BUILD" \
              -G Ninja \
              -DCMAKE_BUILD_TYPE=Release \
              -DCMAKE_INSTALL_PREFIX="$LLVM_INSTALL" \
              -DLLVM_ENABLE_PROJECTS="clang" \
              -DLLVM_TARGETS_TO_BUILD="AArch64" \
              -DLLVM_ENABLE_ASSERTIONS=OFF \
              -DCMAKE_C_COMPILER=clang \
              -DCMAKE_CXX_COMPILER=clang++ \
              -DLLVM_OPTIMIZED_TABLEGEN=ON \
              || fail "LLVM configuration failed"
        ok "LLVM configured"
    fi

    # Step 4: Build
    info "Building LLVM/Clang (this may take a while)..."
    cmake --build "$LLVM_BUILD" -j"$JOBS" || fail "LLVM build failed"
    ok "LLVM/Clang built successfully"

    # Step 5: Install
    info "Installing to $LLVM_INSTALL ..."
    cmake --install "$LLVM_BUILD" || fail "LLVM install failed"
    ok "LLVM/Clang installed"

    # Step 6: Verify OluxOS target
    info "Verifying aarch64-unknown-oluxos target..."
    local CLANG_BIN="$LLVM_INSTALL/bin/clang"
    echo 'int main(){return 0;}' | "$CLANG_BIN" --target=aarch64-unknown-oluxos -ffreestanding -x c - -o /dev/null 2>&1 || {
        warn "OluxOS target test failed — the linker path in the patch may need updating"
        warn "Edit OluxOS.cpp to point linker/oluxos-ld.py to the correct path"
    }
    ok "LLVM/Clang build complete"

    # Export path for busybox build
    export OLUXOS_LLVM_INSTALL="$LLVM_INSTALL"
    echo "$LLVM_INSTALL" > "$WORK_DIR/.llvm_install_path"
}

# ── Main ─────────────────────────────────────────────────────────────────────
main() {
    local TARGET="${1:-all}"

    echo ""
    echo "╔══════════════════════════════════════════════════════════╗"
    echo "║       OluxOS Toolchain Builder                          ║"
    echo "║  LLVM ${LLVM_VERSION}                     ║"
    echo "╚══════════════════════════════════════════════════════════╝"
    echo ""

    check_deps
    mkdir -p "$WORK_DIR" "$OUT_DIR"

    build_llvm

    echo ""
    ok "═══════════════════════════════════════════════════════════"
    ok "  Build complete!"
    ok "═══════════════════════════════════════════════════════════"
    echo ""
    info "Artifacts:"
    info "  LLVM install:  $OUT_DIR/llvm-install/"
    info "  Work dir:      $WORK_DIR/"
    echo ""
}

main "$@"
