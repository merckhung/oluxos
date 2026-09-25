# Porting OluxOS

This guide covers two kinds of porting: moving OluxOS to another AArch64
board, and building more software for it.

## To another AArch64 board

OluxOS finds hardware through the device tree, so a board that follows the
Linux arm64 boot conventions usually needs only drivers, not changes to the
core.

### What the kernel expects

- **Boot protocol.** The loader enters the kernel at EL3, EL2 or EL1 with
  the MMU off, `x0` = the physical address of the DTB, and the Linux arm64
  `Image` header honoured. The image is position independent and may be
  loaded at any 4 KiB-aligned address. U-Boot `booti`, UEFI stubs that
  hand over a DTB, the Raspberry Pi firmware and QEMU `-kernel` all
  qualify. The initramfs goes in `/chosen` (`linux,initrd-start` and
  `linux,initrd-end`).
- **CPU.** An ARMv8.0-A core with 4 KiB pages and at least 39-bit VAs.
  Secondary cores are started through PSCI (`enable-method = "psci"`) or a
  spin table (`"spin-table"`, `cpu-release-addr`).
- **Device tree nodes the core needs:**
  - `/memory` (all banks);
  - `/chosen` (`bootargs`, `stdout-path`);
  - a GICv2 or GICv3 interrupt controller;
  - an `arm,armv8-timer`;
  - a console: `arm,pl011`, or `brcm,bcm2835-aux-uart`.

  Reserved regions are honoured: `/memreserve/` entries and
  `/reserved-memory` children. A `ramoops`-compatible node places the
  crash log (otherwise the top 64 KiB of the first bank is used).
- **Memory layout.** All RAM must lie below 256 GiB physical (the linear
  map size) and not overlap the kernel's VA windows; see
  [ARCHITECTURE.md](ARCHITECTURE.md#memory). memblock tracks up to 64
  memory and 64 reserved regions.

### Steps

1. **Get a shell on the serial console.** If the board uses a PL011 or GIC
   and a PSCI firmware, OluxOS should already boot: pass the DTB and look
   for the banner. Otherwise write the console driver first. It needs
   `DRV_CONSOLE` level, `register_console`, and a polled write path, since
   it runs before interrupts are up. `drivers/serial/bcm2835_aux.c` is a
   small example.
2. **Add missing interrupt controllers or timers** at the `DRV_IRQCHIP` and
   `DRV_TIMER` levels.
3. **Reset and power-off.** PSCI provides both. Otherwise register
   `reboot_ops` from a watchdog or PMIC driver
   (`drivers/watchdog/bcm2835_wdt.c`).
4. **Storage.** Implement a block driver with `blkdev_register`. Partitions,
   the buffer cache and the FAT/ext4 servers then work unchanged. Add
   `respawn` lines for the new device names to `rootfs/etc/init.conf`.
5. **Network.** Implement `net_device_ops` (`xmit`, `poll`) and call
   `netdev_register`. DHCP, IPv6, sockets and the services then work
   unchanged.
6. **Board support files.** For a firmware-loaded board, add a script like
   `scripts/mkrpi4.sh` that lays out the files the loader expects. Add a
   QEMU machine to `tests/qemu/run_tests.py` if QEMU models the board.
7. **Test** the driver set in QEMU where possible. For real hardware,
   follow the bring-up checklist below.

### Hardware bring-up checklist

- [ ] The banner and memory map print; the RAM size matches the board.
- [ ] `nproc` shows every core; `olux-selftest` passes (it covers
  preemption, SMP, signals, memory and IPC).
- [ ] `/proc/interrupts` counts timer and UART interrupts on every CPU.
- [ ] `reboot` and `poweroff` work, the watchdog resets a hung system
  (`echo c > /proc/sysrq-trigger` with `panic=0`, then wait), and
  `/proc/last_kmsg` survives.
- [ ] Storage: raw read/write of a scratch partition compares correctly,
  and a FAT volume survives `poweroff`.
- [ ] Network: DHCP, `ping`, a large `wget` compared with `sha256sum`, and
  an SSH login.
- [ ] `olux-latency -d 60` numbers recorded as the board's baseline.
- [ ] The soak test (`tests/qemu/soak.py` workload, run on the board) for 24
  hours.

## Porting software

The userspace ABI is Linux AArch64. Build **static** binaries against musl:

```bash
toolchains/userspace/out/bin/oluxos-cc -O2 -static -o hello hello.c
```

`oluxos-cc` wraps the cross GCC with musl's headers and libraries
(`toolchains/userspace/build.sh` builds it from the pinned sources in
`third_party/`). For autotools or CMake projects, set `CC=oluxos-cc`,
`--host=aarch64-linux-musl` and `LDFLAGS=-static`.

To ship a program:

- **In the image:** add a directory under `user/prog/<name>/` with its C
  sources. The Makefile builds it into the initramfs, at `/bin/<name>` or at
  the path written in `user/prog/<name>/dest`. For third-party packages, add a
  `build_<name>` function to `toolchains/userspace/build.sh` with a pinned,
  checksummed tarball in `third_party/`, as Dropbear does, and install it
  with `--extra` in the initramfs rule.
- **At run time:** copy the static binary to `/data` and run it from there.

Things that differ from Linux, and what to do about them:

| Linux feature | OluxOS | Do this instead |
|---------------|--------|-----------------|
| Shared libraries, `dlopen` | Not supported | Link statically |
| `inotify`, `fanotify` | Not supported | Poll, or use an IPC channel |
| `epoll` | Not supported | `poll`/`ppoll`/`select` (musl's `epoll` wrappers fail with `ENOSYS`) |
| Namespaces, cgroups, seccomp | Not supported | |
| `/sys` | Not present | `/proc`, `/dev`, and the device ioctls in [SYSCALLS.md](SYSCALLS.md) |
| udev | Devices appear in `/dev` when their driver probes | |
| systemd | `init` with `/etc/init.conf` | Add a `respawn` line |
| Extended attributes, POSIX ACLs | Not supported | |

[SYSCALLS.md](SYSCALLS.md) lists the system calls and ioctls.
