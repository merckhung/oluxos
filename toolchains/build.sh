#!/bin/bash
###############################################################################
# OluxOS Toolchain — Full Build Script
#
# Builds:
#   1. LLVM/Clang 22.1.7 with OluxOS target patch
#      → produces oluxos-clang (native aarch64-unknown-oluxos support)
#   2. Busybox 1.36.1 cross-compiled for aarch64 OluxOS
#      → static ELF64 binary at $OUT_DIR/busybox
#
# Usage:
#   ./build.sh              # build everything
#   ./build.sh llvm         # build LLVM/Clang only
#   ./build.sh busybox      # build busybox only (uses system cross-compiler)
#   ./build.sh busybox-llvm # build busybox with custom LLVM clang
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

BUSYBOX_VERSION="1.36.1"
BUSYBOX_URL="https://busybox.net/downloads/busybox-${BUSYBOX_VERSION}.tar.bz2"
BUSYBOX_CFG="$SCRIPT_DIR/BUSYBOX.cfg"

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

# ── Busybox build ────────────────────────────────────────────────────────────
build_busybox() {
    local USE_LLVM="${1:-no}"
    info "=== Building Busybox ${BUSYBOX_VERSION} for aarch64 OluxOS ==="

    local BUSYBOX_SRC="$WORK_DIR/busybox-${BUSYBOX_VERSION}"
    local CROSS_PREFIX="aarch64-linux-gnu-"
    local CC_CMD="aarch64-linux-gnu-gcc"
    local CXX_CMD="aarch64-linux-gnu-g++"

    # If using custom LLVM clang
    if [ "$USE_LLVM" = "yes" ]; then
        local LLVM_INSTALL=""
        if [ -f "$WORK_DIR/.llvm_install_path" ]; then
            LLVM_INSTALL="$(cat "$WORK_DIR/.llvm_install_path")"
        elif [ -d "$OUT_DIR/llvm-install" ]; then
            LLVM_INSTALL="$OUT_DIR/llvm-install"
        else
            fail "LLVM install not found. Run './build.sh llvm' first."
        fi

        CLANG="$LLVM_INSTALL/bin/clang"
        if [ ! -x "$CLANG" ]; then
            fail "Custom clang not found at $CLANG"
        fi
        CROSS_PREFIX=""
        CC_CMD="$CLANG --target=aarch64-unknown-oluxos"
        info "Using custom LLVM clang: $CLANG"
    fi

    # Step 1: Download source
    if [ -d "$BUSYBOX_SRC" ]; then
        info "Busybox source already present: $BUSYBOX_SRC"
    else
        info "Downloading Busybox ${BUSYBOX_VERSION}..."
        mkdir -p "$WORK_DIR"
        local TAR_FILE="$WORK_DIR/busybox-${BUSYBOX_VERSION}.tar.bz2"
        if [ ! -f "$TAR_FILE" ]; then
            wget -q --show-progress -O "$TAR_FILE" "$BUSYBOX_URL" || fail "Failed to download busybox source"
        fi
        info "Extracting..."
        tar xjf "$TAR_FILE" -C "$WORK_DIR"
        ok "Busybox source extracted"
    fi

    # Step 2: Configure
    if [ ! -f "$BUSYBOX_SRC/.config" ] || [ "$BUSYBOX_CFG" -nt "$BUSYBOX_SRC/.config" ]; then
        info "Configuring busybox..."
        cp "$BUSYBOX_CFG" "$BUSYBOX_SRC/.config"

        # Adjust cross-compiler prefix based on mode
        if [ "$USE_LLVM" = "yes" ]; then
            # For LLVM clang we use EXTRA_CFLAGS to inject target, no prefix
            sed -i 's|^CONFIG_CROSS_COMPILER_PREFIX=".*"|CONFIG_CROSS_COMPILER_PREFIX=""|' "$BUSYBOX_SRC/.config"
            sed -i 's|^CONFIG_EXTRA_CFLAGS=".*"|CONFIG_EXTRA_CFLAGS="--target=aarch64-unknown-oluxos -static"|' "$BUSYBOX_SRC/.config"
        else
            sed -i 's|^CONFIG_CROSS_COMPILER_PREFIX=".*"|CONFIG_CROSS_COMPILER_PREFIX="aarch64-linux-gnu-"|' "$BUSYBOX_SRC/.config"
            sed -i 's|^CONFIG_EXTRA_CFLAGS=".*"|CONFIG_EXTRA_CFLAGS="-static"|' "$BUSYBOX_SRC/.config"
        fi
        sed -i 's|^# CONFIG_STATIC_LIBGCC is not set$|CONFIG_STATIC_LIBGCC=y|' "$BUSYBOX_SRC/.config"

        (cd "$BUSYBOX_SRC" && make silentoldconfig) || fail "Busybox config failed"
        ok "Busybox configured"
    fi

    # Step 3: Build
    info "Building busybox (JOBS=$JOBS)..."
    (
        cd "$BUSYBOX_SRC"
        export CROSS_COMPILE="$CROSS_PREFIX"
        if [ "$USE_LLVM" = "yes" ]; then
            export CC="$CC_CMD"
            export CXX="$CXX_CMD"
        fi
        make -j"$JOBS" clean 2>/dev/null || true
        make -j"$JOBS" || fail "Busybox build failed"
    )
    ok "Busybox built successfully"

    # Step 4: Install
    info "Installing busybox to $OUT_DIR ..."
    cp "$BUSYBOX_SRC/busybox" "$OUT_DIR/busybox"
    chmod +x "$OUT_DIR/busybox"

    # Step 5: Verify
    info "Verifying binary..."
    local ARCH_CHECK
    ARCH_CHECK="$(aarch64-linux-gnu-readelf -h "$OUT_DIR/busybox" 2>/dev/null | grep Machine || true)"
    if echo "$ARCH_CHECK" | grep -qi "aarch64"; then
        ok "Binary is AArch64 ✓"
    else
        fail "Binary is NOT AArch64: $ARCH_CHECK"
    fi

    local STATIC_CHECK
    STATIC_CHECK="$(aarch64-linux-gnu-readelf -d "$OUT_DIR/busybox" 2>&1 || true)"
    if echo "$STATIC_CHECK" | grep -q "no dynamic section"; then
        ok "Binary is statically linked ✓"
    else
        warn "Binary may have dynamic dependencies"
    fi

    ok "Busybox installed: $OUT_DIR/busybox ($(du -h "$OUT_DIR/busybox" | cut -f1))"
}

# ── Main ─────────────────────────────────────────────────────────────────────
main() {
    local TARGET="${1:-all}"

    echo ""
    echo "╔══════════════════════════════════════════════════════════╗"
    echo "║       OluxOS Toolchain Builder                          ║"
    echo "║  LLVM ${LLVM_VERSION} + Busybox ${BUSYBOX_VERSION}       ║"
    echo "╚══════════════════════════════════════════════════════════╝"
    echo ""

    check_deps
    mkdir -p "$WORK_DIR" "$OUT_DIR"

    case "$TARGET" in
        llvm)
            build_llvm
            ;;
        busybox)
            build_busybox no
            ;;
        busybox-llvm)
            build_llvm
            build_busybox yes
            ;;
        all|*)
            build_llvm
            build_busybox no
            ;;
    esac

    echo ""
    ok "═══════════════════════════════════════════════════════════"
    ok "  Build complete!"
    ok "═══════════════════════════════════════════════════════════"
    echo ""
    info "Artifacts:"
    info "  Busybox:       $OUT_DIR/busybox"
    info "  LLVM install:  $OUT_DIR/llvm-install/"
    info "  Work dir:      $WORK_DIR/"
    echo ""
}

main "$@"
