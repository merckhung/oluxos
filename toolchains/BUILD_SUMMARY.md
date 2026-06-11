# OluxOS Toolchain — Build Summary

> Generated: 2026-06-10

## Toolchain Components

| Component | File | Description |
|---|---|---|
| **Clang wrapper** | `oluxos-clang` | Shell wrapper targeting `aarch64-none-elf`; handles compile (`-c`) and full link steps |
| **Custom linker** | `linker/oluxos-ld.py` | Python script wrapping `ld.lld` with aarch64 ELF static-link flags (`-Ttext=0x400000`) |
| **Dynamic loader** | `loader/ld.so.c` | Minimal ELF64 loader — maps `PT_LOAD` segments, jumps to entry point |
| **LLVM/Clang patch** | `llvm-project-oluxos.patch` | 194-line patch adding `OluxOS` OS type to LLVM `Triple` enum + clang toolchain integration |

## Busybox Build

| Detail | Value |
|---|---|
| **Source version** | busybox 1.36.1 |
| **Cross-compiler** | `aarch64-linux-gnu-gcc` (GCC 15, via Ubuntu cross-binutils) |
| **Static libc** | `/usr/aarch64-linux-gnu/lib/libc.a` (glibc, 6.0 MB) |
| **Static libgcc** | `/usr/lib/gcc-cross/aarch64-linux-gnu/15/libgcc.a` |

### Build Artifact

```
busybox: ELF 64-bit LSB executable, ARM aarch64, version 1 (GNU/Linux),
         statically linked, BuildID[sha1]=f0005f8a..., for GNU/Linux 3.7.0, stripped
```

| Property | Value |
|---|---|
| **Architecture** | AArch64 (ELF64, little-endian) |
| **Entry point** | `0x400600` |
| **Size (stripped)** | 1.1 MB |
| **Size (unstripped)** | 1.3 MB |
| **Dynamic deps** | None (fully static) |
| **PT_LOAD segments** | 2 — text at `0x400000`, data+bss at `0x51b150` |

### Applets Included

| Applet | Symbol | Address |
|---|---|---|
| `ash` (shell) | `ash_main` | `0x4b2a10` |
| `cat` | `cat_main` | `0x4b371c` |
| `echo` | `echo_main` | `0x4b3764` |
| `ls` | `ls_main` | `0x4b3db8` |

### Busybox Config Changes (`BUSYBOX.cfg`)

```ini
CONFIG_STATIC=y
CONFIG_CROSS_COMPILER_PREFIX="aarch64-linux-gnu-"
CONFIG_STATIC_LIBGCC=y
CONFIG_EXTRA_CFLAGS="-static"
CONFIG_SHELL_ASH=y
```

## Build Steps (Reproducible)

```bash
# 1. Download source
wget https://busybox.net/downloads/busybox-1.36.1.tar.bz2
tar xjf busybox-1.36.1.tar.bz2

# 2. Copy and adjust config
cp BUSYBOX.cfg busybox-1.36.1/.config

# 3. Validate and build
cd busybox-1.36.1
make silentoldconfig
make -j$(nproc)

# 4. Install
cp busybox ../busybox
```

## Compatibility Notes

- The binary is compatible with the OluxOS loader (`loader/ld.so.c`) — two `PT_LOAD` segments with predictable virtual addresses.
- The `oluxos-clang` wrapper uses `--target=aarch64-none-elf` (freestanding) with `-nostdlib` for compile-only steps, delegating full links to the custom linker.
- For production LLVM integration, apply `llvm-project-oluxos.patch` to clang 22.1.7 source to enable `--target=aarch64-unknown-oluxos` natively.
