# OluxOS

OluxOS is a small, preemptive, SMP operating system kernel for 64-bit ARM,
targeting the **Raspberry Pi 4 Model B** and **QEMU `virt`**. It implements the
Linux AArch64 system-call ABI, so unmodified static musl programs (BusyBox
included) run on it. All hardware is discovered from the device tree.

```
$ make && make run
[    0.000000] OluxOS 0.2.0 AArch64
[    0.131254] GICv2: 288 interrupts
[    0.135890] smp: 4 CPU(s) online
...
root@oluxos:~# uname -a
OluxOS oluxos 0.2.0 #1 SMP aarch64
```

## Features

| Area | What exists |
|------|-------------|
| Boot | Linux arm64 Image protocol (QEMU `-kernel`, Pi firmware `kernel8.img`), position-independent start-up, EL3/EL2→EL1 |
| Memory | Higher-half kernel, linear map of all RAM (incl. >4 GiB), W^X, buddy allocator with DMA zones, slab `kmalloc`, guarded `vmalloc` stacks |
| Processes | Preemptive O(1) priority scheduler (FIFO/RR/OTHER), SMP with IPIs, per-thread FP/SIMD + TLS state, `fork` with copy-on-write, POSIX threads, futexes |
| Protection | Per-process ASIDs, unprivileged (`LDTR/STTR`) user copies, faults kill the process not the system, ASLR, stack guard pages, stack protector |
| Signals | POSIX signals with Linux-compatible frames, job control, `sigaltstack`, restartable syscalls, timers |
| Files | VFS with mounts and symlinks, tmpfs, devtmpfs, procfs, pipes/FIFOs, `poll`/`select`, eventfd, memfd |
| Console | PL011 UART, TTY line discipline, sessions and controlling terminals |
| Userspace | musl 1.2.5 + BusyBox 1.36.1 built from vendored sources; `/sbin/init` with service supervision and watchdog feeding |

See [docs/PRODUCTION_READINESS_PLAN.md](docs/PRODUCTION_READINESS_PLAN.md) for
the roadmap and [docs/](docs/) for design notes.

## Building

Requirements: an AArch64 GCC cross compiler, GNU make, Python 3, and QEMU for
running and testing.

```bash
sudo apt install gcc-aarch64-linux-gnu qemu-system-arm python3 make
make            # kernel (out/Image) + initramfs (out/initramfs.cpio)
make run        # boot in QEMU virt (SMP=4 MEM=1G by default)
make test       # host unit tests + QEMU integration tests
make rpi4       # Raspberry Pi 4 boot files in out/rpi4/
```

No cross compiler? `toolchains/cross/build-cross-gcc.sh` builds one from
source (GCC 13.3 + binutils 2.42 + musl), and `make` picks it up
automatically. See [toolchains/README.md](toolchains/README.md).

Bazel works too: `bazel build //:image //:rpi4_boot`, `bazel run //:qemu`,
`bazel test //:unit_tests //:qemu_tests`.

### Raspberry Pi 4B

`make rpi4` writes a boot directory with `kernel8.img`, the initramfs,
`config.txt`, `cmdline.txt` and the pinned Raspberry Pi firmware. Copy it to a
FAT32-formatted SD card, connect a 3.3 V USB-serial adapter to GPIO 14/15
(pins 8/10), and open it at 115200 8N1.

## Repository layout

```
arch/arm64/     boot, exception vectors, MMU, context switch, user access
kernel/         scheduler, processes, signals, exec, syscalls, time, SMP
mm/             memblock, buddy allocator, kmalloc, vmalloc, address spaces
fs/             VFS, tmpfs, procfs, pipes, poll, initramfs
drivers/        GIC, generic timer, PL011, TTY, random, PSCI, ...
lib/            string, printf, device tree parser
user/prog/      userspace programs (init, olux-selftest, ...)
rootfs/         root filesystem skeleton (/etc)
tests/          host unit tests (tests/unit) and QEMU tests (tests/qemu)
toolchains/     userspace and cross toolchain builders
third_party/    vendored musl and BusyBox source tarballs
legacy/         the original multi-architecture prototype (not built)
```

## Testing

- `make unit-test`: the kernel's string, printf and device-tree code
  compiled natively with AddressSanitizer/UBSan. Includes a mutation fuzzer
  for the DTB parser.
- `make qemu-test`: boots the system and runs `olux-selftest` (42 kernel
  semantics checks: COW, signals, threads, mmap, preemption, and more), then
  shell, job-control, stress and power-off scenarios.
- CI (`.github/workflows/ci.yml`) runs both on every push, across 1/4 CPUs
  and GICv2/GICv3.

## License

The OluxOS sources do not declare a license yet; the copyright holder needs
to choose one before redistribution. Vendored third-party components keep
their own licenses: musl is MIT, BusyBox is GPL-2.0, and the Raspberry Pi
firmware fetched by `scripts/mkrpi4.sh` is under Broadcom's redistribution
licence.
