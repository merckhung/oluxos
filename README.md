# OluxOS

OluxOS is a small, preemptive, SMP operating system kernel for 64-bit ARM,
targeting the **Raspberry Pi 4 Model B** and **QEMU `virt`**. It implements the
Linux AArch64 system-call ABI, so unmodified static musl programs (BusyBox
included) run on it. All hardware is discovered from the device tree.

```
$ make && make run
[    0.000000] OluxOS 0.3.0 AArch64
[    0.131254] GICv2: 288 interrupts
[    0.135890] smp: 4 CPU(s) online
...
root@oluxos:~# uname -a
OluxOS oluxos 0.3.0 #1 SMP aarch64
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
| Storage | Block layer with buffer cache and MBR/GPT partitions; virtio-blk, SD card (EMMC2), USB mass storage; FAT12/16/32 read-write and ext2/3/4 read-only as restartable userspace servers |
| IPC | Message channels with kernel-attested sender identity and descriptor passing; AF_UNIX sockets |
| Networking | lwIP-based TCP/IP (IPv4, IPv6/SLAAC, DHCP, DNS), BSD sockets, virtio-net and Pi 4 GENET; SSH (Dropbear), NTP, syslog, telnet/HTTP (BusyBox) |
| USB | PCIe (ECAM, BCM2711), xHCI, hubs, HID keyboard/mouse (evdev), mass storage |
| Pi 4 hardware | Firmware mailbox, GPIO, I2C, SPI, framebuffer, RNG200, watchdog with reset reason, thermal governor |
| Console | PL011 and mini-UART, TTY line discipline, sessions, pseudo-terminals |
| Lifecycle | A/B updates with Ed25519-signed bundles and firmware `tryboot` rollback; crash log that survives reset (`/proc/last_kmsg`); hardware watchdog; RTC |
| Userspace | musl 1.2.5, BusyBox 1.36.1 and Dropbear 2024.86 built from vendored sources; `/sbin/init` with service supervision and watchdog feeding |

Documentation:

- [Architecture](docs/ARCHITECTURE.md)
- [Driver guide and hardware test status](docs/DRIVERS.md)
- [System-call ABI](docs/SYSCALLS.md)
- [Porting](docs/PORTING.md)
- [Operating a Pi 4 (install, SSH, updates, crash logs)](docs/OPERATIONS.md)
- [Security model](docs/SECURITY.md)
- [Third-party components](THIRD_PARTY.md)
- [Changelog](CHANGELOG.md)
- [Production-readiness plan and status](docs/PRODUCTION_READINESS_PLAN.md)

## Building

Requirements: an AArch64 GCC cross compiler, GNU make, Python 3, and QEMU for
running and testing.

```bash
sudo apt install gcc-aarch64-linux-gnu qemu-system-arm python3 make
make            # kernel (out/Image) + initramfs (out/initramfs.cpio)
make run        # boot in QEMU virt (SMP=4 MEM=1G by default)
make test       # host unit tests + QEMU integration tests
make rpi4       # Raspberry Pi 4 boot files in out/rpi4/
make sdcard     # Raspberry Pi 4 SD card image (A/B layout) in out/sdcard.img
make update     # signed A/B update bundle in out/update.tar
make soak       # long-running load test in QEMU (SOAK_MINUTES=30)
```

No cross compiler? `toolchains/cross/build-cross-gcc.sh` builds one from
source (GCC 13.3 + binutils 2.42 + musl), and `make` picks it up
automatically. See [toolchains/README.md](toolchains/README.md).

Bazel works too: `bazel build //:image //:rpi4_boot`, `bazel run //:qemu`,
`bazel test //:unit_tests //:qemu_tests`.

### Raspberry Pi 4B

`make sdcard` writes `out/sdcard.img`: an `autoboot.txt` partition, two boot
slots (firmware, `kernel8.img`, initramfs, configuration) and a data
partition. Write it to a card with `dd`, connect a 3.3 V USB-serial adapter
to GPIO 14/15 (pins 8/10) at 115200 8N1, or log in over SSH with a key
placed in `authorized_keys` on the boot partition. See
[docs/OPERATIONS.md](docs/OPERATIONS.md). `make rpi4` writes just the boot
files, for a single FAT32 partition.

Drivers for hardware that QEMU does not model (GENET Ethernet, PCIe/VL805
USB, RNG200) have not yet been run on a physical board; see
[docs/DRIVERS.md](docs/DRIVERS.md).

## Repository layout

```
arch/arm64/     boot, exception vectors, MMU, context switch, user access
kernel/         scheduler, processes, signals, exec, syscalls, time, SMP, IPC, pstore
mm/             memblock, buddy allocator, kmalloc, vmalloc, address spaces
fs/             VFS, tmpfs, devtmpfs, procfs, pipes, poll, initramfs, userfs client
net/            socket layer, AF_UNIX, AF_INET/AF_INET6 over lwIP, network devices
drivers/        interrupt controller, timer, serial, block, SD, virtio, PCI, USB, input,
                network, GPIO, I2C, SPI, video, watchdog, RTC, firmware
lib/            string, printf, device tree parser
user/prog/      userspace programs (init, fatfsd, ext4fsd, netcfg, olux-update, ...)
rootfs/         root filesystem skeleton (/etc, scripts)
tests/          host unit tests (tests/unit), QEMU tests and soak test (tests/qemu)
toolchains/     userspace and cross toolchain builders
third_party/    vendored musl, BusyBox, Dropbear and lwIP sources
docs/           architecture, drivers, ABI, porting, operations, security
legacy/         the original multi-architecture prototype (not built)
```

## Testing

- `make unit-test` compiles the kernel's string, printf and device-tree code
  natively with AddressSanitizer and UBSan. It includes a mutation fuzzer
  for the DTB parser.
- `make qemu-test` boots the system and runs `olux-selftest` (51 checks of
  kernel semantics: COW, signals, threads, mmap, preemption, sockets,
  PTYs, and more). It then runs about 30 scenarios:
  - shell and job control;
  - storage and filesystem servers, including crash and restart;
  - networking (DHCP, IPv6, HTTP, telnet, SSH);
  - NTP, USB (typing, mouse, a drive behind a hub, hot-unplug), RTC,
    latency;
  - power-off, watchdog reset, crash recovery via pstore, persistence
    across boots.

  With `--machine raspi4b` it also covers the Pi 4 drivers, the SD card
  image and the A/B update flow.
- `make soak` runs a long load test that watches for hangs, panics and
  memory leaks.
- CI (`.github/workflows/ci.yml`) runs the unit tests and the QEMU suite
  on every push: `virt` with 1 or 4 CPUs and GICv2 or GICv3, and
  `raspi4b`.

## License

The OluxOS sources do not declare a license yet; the copyright holder needs
to choose one before redistribution. Vendored third-party components keep
their own licenses (musl: MIT, BusyBox: GPL-2.0, Dropbear: MIT-style, lwIP:
BSD-3-Clause; the Raspberry Pi firmware fetched by `scripts/mkrpi4.sh` is
under Broadcom's redistribution licence). See [THIRD_PARTY.md](THIRD_PARTY.md)
for versions, checksums and obligations.
