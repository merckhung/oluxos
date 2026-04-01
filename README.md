# OluxOS

OluxOS is a minimalist operating system kernel. This document provides instructions on how to build the kernel and boot it using QEMU, specifically using the **Bazel** build system.

## Prerequisites

To build and run OluxOS with Bazel, you will need:

- **Bazel** (or `bazelisk`)
- **GCC** (for x86/IA32 cross-compilation)
- **binutils** (`ld`, `as`, `objcopy`, etc.)
- **QEMU** (specifically `qemu-system-i386`)
- **dd** (usually pre-installed on Unix-like systems)

## Building the Kernel Image

To build the bootable floppy image (`OluxOS.img`), run:

```bash
bazel build //:OluxOS_img
```

This command will:
1. Compile the kernel and boot sector.
2. Build the `krnimg` utility.
3. Generate the `OluxOS.krn` binary.
4. Create a 1.44MB `OluxOS.img` floppy image.

The output image will be located in `bazel-bin/OluxOS.img`.

## Booting with QEMU

You can launch the kernel directly using Bazel:

```bash
bazel run //:run_qemu
```

This target builds the image (if necessary) and executes the `run_qemu.sh` script with the appropriate arguments to start `qemu-system-i386`.

## Cleaning Up

To remove all Bazel-generated files:

```bash
bazel clean
```

## Architecture and Configuration

The Bazel build is currently configured for **IA32**. Configuration flags and common compiler options are defined in the root `BUILD.bazel` file under `COMMON_COPTS` and `COMMON_DEFINES`.
