# Changelog

OluxOS follows [semantic versioning](https://semver.org/). Before 1.0 the
kernel ABI extensions (system calls 1000 and up, the userfs protocol) may
still change between minor versions. The Linux-compatible ABI does not
change incompatibly.

## 0.3.0 (unreleased)

Storage, networking, USB, Raspberry Pi 4 platform support, and field
lifecycle.

### Added

**Storage**
- Block layer with a write-back buffer cache and MBR and GPT partitions.
- virtio-mmio transport with virtio-blk, virtio-net and virtio-rng.
- EMMC2 SD card driver.

**Filesystems and IPC**
- IPC channels (`olux_channel`, `olux_msg_send`, `olux_msg_recv`) with
  kernel-attested sender identity and descriptor passing.
- Userspace filesystem servers: `fatfsd` (FAT12/16/32 read-write, long
  names, repair after unclean shutdown) and `ext4fsd` (ext2/3/4
  read-only), supervised and restartable without losing the mount.

**Networking**
- TCP/IP with lwIP 2.2.0: IPv4, IPv6 with SLAAC, DHCP and DNS.
- BSD sockets, AF_UNIX, and pseudo-terminals.
- NIC drivers: virtio-net and Pi 4 GENET v5.
- Services: Dropbear SSH (key-only logins), `syslogd`, NTP through
  `adjtimex` clock discipline, BusyBox networking applets.

**Raspberry Pi 4 platform**
- Firmware mailbox (`/dev/vcio`), GPIO (`/dev/gpiochip0`), I2C (i2c-dev),
  SPI (spidev), framebuffer, RNG200.
- PM watchdog with the reset reason and power-off.
- Thermal governor.

**USB**
- PCI core with ECAM, and the BCM2711 PCIe root complex (VL805).
- xHCI, hubs, HID keyboards and mice (evdev, console input), mass storage.

**Lifecycle**
- A/B updates using the firmware's `autoboot.txt`/`tryboot`: `olux-update`,
  Ed25519-signed bundles (`make update`, `olux-verify`).
- SD card image with the A/B layout (`make sdcard`).
- pstore: the log of the previous boot survives a warm reset
  (`/proc/last_kmsg`), and the boot reports how that boot ended.
- `/proc/sysrq-trigger` (`c`, `b`, `o`, `s`, `t`).
- RTC core with `/dev/rtc0` (`hwclock`), and the PL031 driver.
- `olux-latency` (timer and IPC latency), the soak test (`make soak`), and
  `Buffers` in `/proc/meminfo`.

**Documentation**
- Architecture, driver guide with test status, syscall ABI, porting,
  operations, security model, third-party inventory.

### Fixed
- `nanosleep`/`clock_nanosleep` could sleep for about 146 years when the
  deadline passed between two clock reads.
- A TCP connection closed while its send queue was full was reset instead
  of delivering the queued data.
- `O_NOCTTY` was dropped before the driver's open, which broke controlling
  terminals on PTYs.
- A use-after-free when an AF_UNIX datagram peer closed.
- Ignored signals were discarded even while a thread was blocking or
  waiting for them; `sigtimedwait` did not unblock the waited-for set.
- SSH keys on `/boot` and `/data` were read before those filesystems were
  mounted.

### Removed
- The `olux_sysinfo` and `olux_watchdog` placeholder system calls (numbers
  1003 and 1004 stay reserved).

## 0.2.0

A new AArch64 kernel replacing the prototype:

- Linux ABI; higher-half kernel; W^X.
- SMP with a preemptive O(1) priority scheduler.
- Processes and threads, COW `fork`, signals, TTY and job control, VFS
  with tmpfs, devtmpfs and procfs.
- musl and BusyBox built from pinned sources.
- QEMU test suite and CI; Raspberry Pi 4 boot files.
