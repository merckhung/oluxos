# Third-party components (software bill of materials)

Everything OluxOS ships, other than its own sources, comes from the pinned
upstream releases below. The build verifies each archive's SHA-256 before
using it.

## In the images

| Component | Version | Where | Used for | License | Source and checksum |
|-----------|---------|-------|----------|---------|---------------------|
| musl libc | 1.2.5 | `third_party/musl/musl-1.2.5.tar.gz` | C library of every userspace program | MIT | musl.libc.org; sha256 `a9a118bbe84d8764da0ea0d28b3ab3fae8477fc7e4085d90102b8596fc7c75e4` |
| BusyBox | 1.36.1 | `third_party/busybox/busybox-1.36.1.tar.bz2` | Shell and utilities (configuration in `toolchains/userspace/busybox.config`) | GPL-2.0-only | busybox.net; sha256 `b8cc24c9574d809e7279c3be349795c5d5ceb6fdf19ca709f80cde50e47de314` |
| Dropbear SSH | 2024.86 | `third_party/dropbear/dropbear-2024.86.tar.bz2` | SSH server and client, `dropbearkey`, `scp` | MIT-style (Dropbear), with bundled libtomcrypt/libtommath (public domain / Unlicense) | matt.ucc.asn.au/dropbear; sha256 `f933205a1e98b98810fcc5116cf97bdb6065c28bad526ff42f7eaf1bd2943ea6` |
| lwIP | 2.2.0 | `third_party/lwip/src` (vendored tree) | TCP/IP stack inside the kernel | BSD-3-Clause (`third_party/lwip/COPYING`) | savannah.nongnu.org/projects/lwip, tag `STABLE-2_2_0_RELEASE`, commit `0a0452b2c39bdd91e252aef045c115f88f6ca773`; no local changes |
| Raspberry Pi firmware | 1.20250430 | Downloaded by `scripts/mkrpi4.sh` into the Pi boot files | `start4.elf`, `fixup4.dat`, the Pi 4 device tree, `disable-bt` overlay | Broadcom redistribution license (in the firmware repository's `boot/LICENCE.broadcom`) | github.com/raspberrypi/firmware; per-file SHA-256 in `scripts/mkrpi4.sh` |
| libgcc | That of the cross compiler (13.3.0 from `toolchains/cross/build-cross-gcc.sh`, or the distribution's) | Linked into the userspace binaries | Compiler runtime helpers | GPL-3.0 with the GCC Runtime Library Exception | gcc.gnu.org |

Code derived from other sources inside the OluxOS tree:

| Code | Origin | License |
|------|--------|---------|
| Ed25519 field and group arithmetic in `user/prog/olux-verify/olux-verify.c` | TweetNaCl (Bernstein, van Gastel, Janssen, Lange, Schwabe, Smetsers) | Public domain |
| Civil-date conversion in `drivers/rtc/rtc.c` | Howard Hinnant's `days_from_civil` algorithms | Public domain (algorithm description) |

The kernel drivers follow the register sequences of the corresponding Linux
drivers and hardware documentation. They are independent implementations
and contain no Linux code.

## Build tools only (not shipped)

| Tool | Version | Script |
|------|---------|--------|
| GCC | 13.3.0 | `toolchains/cross/build-cross-gcc.sh` (optional: a host `aarch64-linux-gnu-gcc` also works) |
| binutils | 2.42 | same |
| QEMU | 8.2 (virt), 9.x (raspi4b) | Test only; `scripts/ci/install-qemu9.sh` |
| mtools, dosfstools, e2fsprogs, dtc, OpenSSL | Distribution packages | Image building and update signing |

## License obligations for products

- **BusyBox is GPL-2.0.** Anyone who distributes a device image must offer
  the corresponding source: the BusyBox tarball above plus
  `toolchains/userspace/busybox.config` and `toolchains/userspace/build.sh`.
- **Attribution.** The MIT and BSD components (musl, Dropbear, lwIP) require
  their license texts to accompany binary distributions.
- **Raspberry Pi firmware** may be redistributed only with Raspberry Pi
  hardware, under Broadcom's terms.
- **OluxOS itself** does not declare a license yet (see the README).
