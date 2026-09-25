# OluxOS toolchains

Everything needed to build OluxOS userspace comes from source that is either
in this repository or fetched by a script and checked against a pinned SHA-256.
No prebuilt target binaries are committed.

| Directory | What it builds | When to use it |
|-----------|----------------|----------------|
| `userspace/` | musl 1.2.5 sysroot, the `oluxos-cc` compiler wrapper and BusyBox 1.36.1 (static) | Always. Bazel runs it for `//:busybox_elf` and `//:rootfs_img`. |
| `cross/` | A complete `aarch64-linux-musl` GCC 13.3 + binutils 2.42 cross toolchain | The host has no AArch64 cross compiler package. |
| `llvm/` | An experimental Clang with an `aarch64-unknown-oluxos` triple | Optional research only; not used by the build. |

## Vendored sources (`third_party/`)

| Package | File | SHA-256 |
|---------|------|---------|
| musl 1.2.5 | `third_party/musl/musl-1.2.5.tar.gz` | `a9a118bbe84d8764da0ea0d28b3ab3fae8477fc7e4085d90102b8596fc7c75e4` |
| BusyBox 1.36.1 | `third_party/busybox/busybox-1.36.1.tar.bz2` | `b8cc24c9574d809e7279c3be349795c5d5ceb6fdf19ca709f80cde50e47de314` |

These are the unmodified upstream release tarballs. Local changes live as
patches under `userspace/patches/`.

## Quick start

```bash
# Option A: the host has a packaged cross compiler (Debian/Ubuntu)
sudo apt install gcc-aarch64-linux-gnu mtools dosfstools qemu-system-arm
toolchains/userspace/build.sh            # -> toolchains/userspace/out/bin/{busybox,oluxos-cc}

# Option B: build the cross compiler from source first (20-60 min)
toolchains/cross/build-cross-gcc.sh      # -> toolchains/cross/install/bin/aarch64-linux-musl-gcc
toolchains/userspace/build.sh            # picks up the in-tree toolchain automatically
```

`toolchains/userspace/build.sh` needs no network access. `build-cross-gcc.sh`
downloads the GCC, binutils, GMP, MPFR and MPC tarballs into `cross/sources/`,
trying the GNU mirrors first, then the Ubuntu archive, then the GCC git mirror
for GCC. To build offline, pre-populate `cross/sources/`.

## Compiling your own programs

```bash
toolchains/userspace/out/bin/oluxos-cc -O2 -o hello hello.c   # static AArch64 ELF
```

The resulting binaries use the Linux AArch64 system call ABI implemented by the
OluxOS kernel, so they also run under `qemu-aarch64` on a Linux host. The unit
tests rely on this.

## BusyBox configuration

`userspace/busybox.config` is a curated applet set: ash with job control and
line editing, coreutils, grep, sed, awk, find, vi, less, ps and top. To change
it, extract the tarball, copy the config in, run `make menuconfig`, then copy
the resulting `.config` back.
