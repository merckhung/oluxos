# Operating OluxOS on a Raspberry Pi 4

This guide covers a Pi 4 in the field: installing it, logging in, keeping
the time, updating it, and finding out what happened after a crash. Most of
it also applies to QEMU `virt`.

## Install

```bash
make sdcard                               # out/sdcard.img, 512 MiB, A/B layout
sudo dd if=out/sdcard.img of=/dev/sdX bs=4M conv=fsync
```

The card has four partitions:

| Partition | Label | Contents | Mounted at |
|-----------|-------|----------|-----------|
| p1 | `OLUXAUTO` (FAT16) | `autoboot.txt`: which slot boots normally and which one a trial boot tries | only while `olux-update` changes it |
| p2 | `OLUXBOOTA` (FAT32) | Slot A: firmware, `config.txt`, `kernel8.img`, `initramfs.cpio`, `cmdline.txt` (with `olux.slot=a`), `VERSION` | `/boot` when slot A runs |
| p3 | `OLUXBOOTB` (FAT32) | Slot B: the same, with `olux.slot=b` | `/boot` when slot B runs |
| p4 | `OLUXDATA` (FAT32) | Your data, SSH host keys and keys you add | `/data` |

The root filesystem is the initramfs. It is read from the boot slot and
held in RAM, so every boot starts clean. Anything that must persist goes
in `/data`. You can grow p4 to fill the card with any partitioning tool.

Needs a bootloader EEPROM from 2021 or later (`tryboot` A/B support). Pi 4
boards sold since then have one. Otherwise update it with Raspberry Pi
Imager's "bootloader" image.

**Serial console:** GPIO 14/15 (pins 8 and 10), 115200 8N1, 3.3 V levels.
A USB keyboard also types into the console.

## Log in

- **Console:** a root shell on the serial port.
- **SSH:** key-based only, and root only.
  - Put your public key in `authorized_keys` on the boot partition (visible
    from any PC) or in `/data/ssh/authorized_keys`. Then `ssh root@<address>`.
  - The host key is created on first boot and kept in `/data/ssh`, so it
    survives reboots and updates.
- **Network:** every Ethernet interface asks for a DHCP lease at boot
  (`/etc/init.d/S10network`). IPv6 configures itself by SLAAC. To manage
  it by hand:
  ```sh
  netcfg                                   # show interfaces
  netcfg eth0 static 192.168.1.50/24 192.168.1.1 1.1.1.1
  netcfg eth0 dhcp
  ```
  To make a static setup permanent, put the `netcfg` line in a script on
  `/data` and call it from your application's start-up.

## Time

The Pi 4 has no battery-backed clock, so the time starts at 1970 until NTP
sets it:

- `ntp-client` starts BusyBox `ntpd` against the servers in
  `/etc/ntp.conf` (`pool.ntp.org`) as soon as there is a default route.
- The kernel then slews the clock like Linux does.
- An RTC is used automatically when the device tree has one (PL031 on QEMU,
  for example). `hwclock` works with `/dev/rtc0`.

## Services and logs

- **Services.** `/etc/init.conf` lists them. `init` restarts services that
  exit, with back-off. It gives up on a service that exits with status 78
  ("not applicable here", for example a filesystem server for a missing
  partition).
- **System log.** `syslogd` keeps `/var/log/messages` in RAM, rotated at
  200 KiB. `logger` and `dmesg` work as usual.
- **Watchdog.** `init` feeds the hardware watchdog (the BCM2835 PM
  watchdog, which resets the board if the system hangs).
  - Boot with `init.watchdog=0` to leave `/dev/watchdog` free for your own
    daemon, such as BusyBox `watchdog` or your application.
  - The previous reset reason is logged at boot: `bcm2835-pm: last reset: ...`.

## After a crash

The kernel mirrors its log into RAM that survives a reset (pstore). At the
next boot:

```
pstore: previous boot #7 ended with a kernel panic; its log is in /proc/last_kmsg
```

The possible endings are:
- a reboot;
- a halt or power-off;
- a kernel panic;
- a watchdog reset;
- an unexpected reset (hang, hardware watchdog or reset button).

A power cut clears the RAM, and then there is no previous log.

```sh
cat /proc/last_kmsg                        # the previous boot's log, including any panic backtrace
cp /proc/last_kmsg /data/crash-$(date +%s).txt
```

A kernel panic reboots after 10 seconds. Change the delay with `panic=N` on
the kernel command line (`cmdline.txt`); `panic=0` halts instead.

**A hung system** that still has a shell:

```sh
echo t > /proc/sysrq-trigger   # every thread, its state and kernel backtrace; timer queues
echo s > /proc/sysrq-trigger   # emergency sync
echo b > /proc/sysrq-trigger   # reset now
```

## Updates (A/B)

An update replaces the whole boot slot (kernel, initramfs, firmware and
configuration) with a signed bundle. The running system is never touched,
so a bad update cannot break it.

1. **Build and sign** on your build machine:
   ```bash
   make update UPDATE_KEY=/secure/oluxos-update.pem    # -> out/update.tar
   ```
   `UPDATE_KEY` is your Ed25519 signing key (`openssl genpkey -algorithm
   ed25519`). Its public half is built into every image as
   `/etc/olux/update.pub`, so **build the images you ship with the same
   key**. Without `UPDATE_KEY`, a development key is generated in
   `out/keys/`. Never ship images built with it.
2. **Install** on the device. Copy the bundle (with `scp`, to `/data`, or
   anywhere), then:
   ```sh
   olux-update install /data/update.tar   # verifies the signature and hashes, writes the other slot
   olux-update status
   olux-update try                        # reboot once into the new slot
   ```
3. **Confirm or fall back.**
   - If the new slot comes up and stays up for `CONFIRM_DELAY` seconds (60,
     set in `/etc/olux/update.conf`), `init`'s `olux-update confirm` makes
     it the default.
   - If it panics, hangs (watchdog) or is reset before that, the next boot
     is the old slot, because the firmware's `tryboot` flag lasts one boot.
   - To go back later, run `olux-update rollback` and reboot.

`olux-update install` refuses bundles with a bad signature or files that do
not match the signed manifest. It installs only the files named in the
manifest, then reads the slot back and checks the hashes again.

## Health

| Check | Command |
|-------|---------|
| Temperature, throttling, clocks | `rpi-info`, `rpi-info temp`, `rpi-info throttled` |
| Thermal governor | `dmesg \| grep rpi-thermal`. Above 80 °C (set with `rpi_thermal.trip=`) the ARM clock is capped until the SoC cools by 5 °C. Under-voltage warnings mean the power supply is too weak |
| Memory | `free`, `/proc/meminfo` (`Buffers` is the block cache, which is reclaimed as needed) |
| Latency | `olux-latency -d 60 -l 500` (fails if a timer wake-up is ever later than 500 µs) |
| Storage | `dmesg \| grep -E 'mmc\|fatfsd'`. `fatfsd` repairs the FAT after an unclean shutdown and logs what it fixed |

## Load testing

`make soak SOAK_MINUTES=600` runs the soak test in QEMU:
- file churn on the FAT server, HTTP requests, and process churn;
- the filesystem server is killed and must be restarted every few minutes;
- the test fails on a panic, a data mismatch, a hang or a memory leak.

Run the equivalent on hardware before a release.
