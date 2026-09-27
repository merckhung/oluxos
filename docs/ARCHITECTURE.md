# OluxOS architecture

OluxOS is a 64-bit ARM kernel for the Raspberry Pi 4B and QEMU `virt`. It
speaks the Linux AArch64 system-call ABI, so static musl binaries (BusyBox,
Dropbear, the OluxOS tools) run unmodified. Hardware is found through the
device tree. This document follows a boot from reset to the shell, then
describes each subsystem. Paths are relative to the repository root.

- [Source layout](#source-layout)
- [Boot flow](#boot-flow)
- [Memory](#memory)
- [Threads, scheduling and time](#threads-scheduling-and-time)
- [System calls and user memory](#system-calls-and-user-memory)
- [Files: VFS, block layer and filesystem servers](#files)
- [IPC channels](#ipc-channels)
- [Networking](#networking)
- [Devices](#devices)
- [Userspace and services](#userspace-and-services)
- [Reliability and lifecycle](#reliability-and-lifecycle)
- [Design decisions and deviations from the plan](#design-decisions)

## Source layout

| Directory | Contents |
|-----------|----------|
| `arch/arm64/` | Entry (`head.S`), exception vectors (`entry.S`), context switch, MMU set-up, traps, user-copy primitives |
| `kernel/` | Start-up (`main.c`, `init.c`), scheduler, processes, signals, exec, futexes, time, IPC channels, printk, panic, pstore, reboot, syscall table |
| `mm/` | memblock, buddy page allocator, `kmalloc`, `vmalloc`, address spaces (VMAs, COW, `mmap`) |
| `fs/` | VFS, tmpfs, devtmpfs, procfs, pipes, `poll`, initramfs unpacking, userfs client |
| `net/` | Socket layer, AF_UNIX, AF_INET/AF_INET6 over lwIP, network devices |
| `drivers/` | One directory per class: `irqchip`, `timer`, `serial`, `firmware`, `block`, `mmc`, `virtio`, `pci`, `usb`, `input`, `net`, `gpio`, `i2c`, `spi`, `video`, `watchdog`, `rtc`, `char` |
| `lib/` | Flattened device tree parser, string and `printf` routines |
| `include/olux/` | Kernel headers; `include/uapi/olux/` holds the userspace ABI headers |
| `user/` | OluxOS programs (`init`, the filesystem servers, tools) and their small support library |
| `rootfs/` | Files copied into the initramfs (`/etc`, scripts) |
| `third_party/` | Pinned sources: musl, BusyBox, Dropbear, lwIP |
| `toolchains/` | Cross-compiler and userspace (musl + BusyBox + Dropbear) build scripts |
| `tests/` | Host unit tests, the QEMU integration suite, the soak test |
| `scripts/` | Image builders (`mkrpi4.sh`, `mksdcard.sh`, `mkupdate.sh`, `mkdisk.sh`, `mkinitramfs.py`) |
| `legacy/` | The earlier multi-architecture prototype, kept for reference; not built |

## Boot flow

1. **Firmware.** QEMU (`-kernel Image`) or the Pi firmware (`kernel8.img`)
   loads the image following the Linux arm64 boot protocol. `x0` holds the
   physical address of the device tree, which also describes the initramfs
   in `/chosen`. The image header says "place anywhere" (4 KiB aligned).
2. **`arch/arm64/head.S`.** Runs position-independent code with the MMU
   off. It drops from EL3 or EL2 to EL1. At EL2 it configures `CNTHCTL_EL2`
   so EL1 may use the physical timer, and it enables the GICv3 system
   registers where present. It then builds early page tables for the kernel
   image (at `KIMAGE_VADDR`) and a fixmap window for the device tree, turns
   on the MMU and caches, and jumps to `start_kernel`.
3. **`kernel/main.c: start_kernel`.** Parses the device tree and starts the
   early console (PL011), prints the banner and command line, then:
   - registers memory and reservations with memblock (kernel image, DTB,
     initrd, `/reserved-memory`, the pstore region);
   - creates the linear map of all RAM (`mmu_init`);
   - starts the page and slab allocators;
   - starts pstore, which mirrors the log and reports how the previous boot
     ended;
   - starts the scheduler;
   - probes the drivers in the first three levels (interrupt controller,
     timer, console);
   - creates the `init` kernel thread;
   - enters the idle loop.
4. **`kernel/init.c: kernel_init`.** Brings up the secondary CPUs (PSCI
   `CPU_ON` on QEMU, the spin table on the Pi). It then probes the remaining
   device-tree driver levels in order (`DRV_FIRMWARE`, `DRV_BUS`,
   `DRV_DEVICE`) and runs the initcalls. It unpacks the initramfs into the
   root tmpfs, mounts devtmpfs and procfs, and executes `/sbin/init` as PID 1.
5. **`/sbin/init`** (`user/prog/init`) runs `/etc/init.d/rcS`, starts the
   services in `/etc/init.conf`, supervises them, and feeds the watchdog.

Drivers bind by device-tree `compatible` string with
`DT_DRIVER(name, level, probe, "compat", ...)`. The level orders probing,
so a driver can rely on everything in the levels before its own. See
[DRIVERS.md](DRIVERS.md).

## Memory

**Virtual layout** (39-bit VAs, 4 KiB pages; `arch/arm64/include/asm/memory.h`):

| Range | Use |
|-------|-----|
| `0x0000000000000000`–`0x0000007fffffffff` | User space (TTBR0, per process, tagged with an ASID) |
| `0xffffff8000000000`–`0xffffffbfffffffff` | Linear map of all RAM (256 GiB window, so 8 GB boards fit) |
| `0xffffffc000000000`–`0xfffffffeffffffff` | `vmalloc`, `ioremap`, kernel stacks with guard pages |
| `0xffffffff40000000`–… | Fixmap (device tree window, temporary mappings) |
| `0xffffffff80000000`–… | Kernel image: text RX, rodata R, data and bss RW-XN (W^X) |

**Physical memory.** memblock collects the RAM banks and reservations from
the device tree. The buddy allocator (`mm/page_alloc.c`) keeps three zones:
`GFP_DMA` below 1 GiB (the BCM2711 legacy DMA limit), `GFP_DMA32` below
4 GiB, and normal memory. Each page has a `struct page` with a reference
count, so COW and shared mappings are safe. `kmalloc` is a slab allocator
with size classes. Debug builds poison freed memory.

**Address spaces** (`mm/vm.c`). An address space is a list of VMAs.
Anonymous memory is demand-zero. `fork` is copy-on-write. `mmap`,
`munmap`, `mprotect`, `mremap` and `MAP_SHARED` anonymous or memfd mappings
are supported. Stacks have guard gaps, and stack and `mmap` bases are
randomised (ASLR, up to 256 MiB). TLB maintenance uses ASIDs, so a context
switch needs no global flush.

**DMA.** `dma_alloc_coherent` returns memory mapped non-cacheable.
Streaming DMA uses the cache maintenance helpers. Bus addresses come from
the device tree's `dma-ranges` (`dt_dma_addr`); for PCI devices they come
from the host bridge (`pci_bus_addr`).

## Threads, scheduling and time

- A **process** (`struct process`) owns the address space, descriptor
  table, credentials, signal handlers and its children. A **thread**
  (`struct thread`) owns its register context, FP/SIMD state, `TPIDR_EL0`,
  signal mask and a 16 KiB kernel stack with a guard page. `clone`
  supports threads (`CLONE_VM|CLONE_THREAD`), and futexes back pthreads.
- **Scheduler** (`kernel/sched.c`). Each CPU has 32 priority run queues and
  a bitmap, so picking the next thread is O(1). SCHED_FIFO, SCHED_RR and
  SCHED_OTHER (with nice) are supported. User code is preempted on the
  250 Hz tick or when a higher-priority thread wakes. Kernel code is not
  preempted.
- **Big kernel lock.** System calls run under one kernel lock (`lock_kernel`),
  which is dropped whenever a thread sleeps. Interrupt handlers, the
  scheduler, timers, the page allocator and the drivers' own data use
  spinlocks, so the four cores run user code in parallel and serialise only
  inside system calls. Splitting the lock is future work.
- A wake-up goes to the thread's previous CPU if that CPU is idle,
  otherwise to an idle CPU, otherwise to the least loaded one, with a
  reschedule IPI when the woken thread should preempt.
- **Time** (`kernel/time.c`). The ARM generic timer provides the
  monotonic clock (virtual counter) and per-CPU one-shot events (absolute
  `CNTV_CVAL`, so there is no drift). Each CPU keeps a sorted list of
  `ktimer`s plus its tick. The realtime clock is monotonic time plus an
  offset. It is disciplined like Linux's: `adjtimex` slews it at up to
  500 ppm and applies a frequency correction, which is what BusyBox `ntpd`
  uses. An RTC, when present, sets the time at boot and is written back
  when the time is stepped.
- The idle loop executes `wfi`.

## System calls and user memory

`svc #0` enters `el0_sync` (`arch/arm64/entry.S`), and `do_syscall`
dispatches on `x8` through the table in `kernel/syscall.c` (Linux numbers).
OluxOS extensions start at 1000. See [SYSCALLS.md](SYSCALLS.md) for the
list.

User pointers are only accessed through `copy_from_user`, `copy_to_user`
and `strncpy_from_user`. These use the unprivileged `LDTR`/`STTR`
instructions, so the MMU applies the user's permissions, and an exception
table turns a fault into `-EFAULT`. A fault in user mode sends the signal
Linux would (SIGSEGV, SIGBUS, SIGILL, SIGFPE). A fault in the kernel
panics with a register dump and a symbolised backtrace.

**Signals** (`kernel/signal.c`) use Linux-compatible frames, `sigaltstack`,
restartable system calls, job control (SIGTSTP, SIGCONT, SIGTTIN) and
`rt_sigtimedwait`. Interval timers are supported.

<a id="files"></a>
## Files: VFS, block layer and filesystem servers

- **VFS** (`fs/vfs.c`) provides dentries and inodes, a mount table, path
  walking with symlinks and `..`, per-process cwd and root, shared open file
  descriptions, `O_APPEND`/`O_CREAT`/`O_TRUNC`/`O_NOCTTY`, and
  `pread`/`pwrite`.
- **In-kernel filesystems**:
  - tmpfs: the root and `/tmp`;
  - devtmpfs: `/dev`, populated by drivers through `devfs_create`;
  - procfs: `/proc`, including per-process files, `meminfo`, `kmsg`,
    `last_kmsg`, `sysrq-trigger` and `net/*`;
  - pipes and FIFOs.
- **Block layer** (`drivers/block/blkdev.c`):
  - a buffer cache of 4 KiB blocks, LRU, capped at 1/16 of RAM, reported as
    `Buffers` in `/proc/meminfo`;
  - dirty blocks written back by the `bflush` thread and on `sync`;
  - MBR and GPT partitions as `vda1`, `mmcblk0p1`, `sda1` and so on;
  - block-special files for raw access.
- **Filesystem servers.** FAT12/16/32 (read-write, long names) and
  ext2/3/4 (read-only) run as **userspace processes**: `/sbin/fatfsd` and
  `/sbin/ext4fsd`.
  - The kernel's userfs client (`fs/userfs.c`) forwards VFS operations over
    an IPC channel using the protocol in `include/uapi/olux/userfs.h`.
  - `init` supervises the servers. When one crashes, the replacement
    re-attaches to the same mount point, and inode numbers stay stable, so
    open files survive with an error on the in-flight request only.
  - `fatfsd` keeps the FAT "clean" flag and repairs lost clusters after an
    unclean shutdown (fsck-lite).

## IPC channels

`olux_channel(2)` creates a connected pair of message endpoints; both are
file descriptors (`include/uapi/olux/ipc.h`).

- Messages are datagrams of up to 64 KiB and can carry up to 8 file
  descriptors, which act as capabilities.
- The kernel stamps each message with the sender's pid, uid and gid, which
  the receiver can trust.
- Endpoints work with `poll`.

The userfs protocol runs over channels, and services can use them for their
own protocols. AF_UNIX sockets (stream and datagram, with `SCM_RIGHTS` and
`SO_PEERCRED`) are also available for standard software.

## Networking

The TCP/IP stack is **lwIP 2.2.0 running inside the kernel**
(`third_party/lwip`, port in `net/lwip/port`).

- **Locking.** lwIP runs with `NO_SYS=1` under one `net_lock` mutex. The
  `netd` kernel thread drives the lwIP timers and the drivers' receive
  queues.
- **Sockets.** `net/socket.c` is the generic BSD socket layer.
  `net/inet.c` implements AF_INET and AF_INET6 on top of lwIP:
  - TCP, UDP, raw ICMP/ICMPv6;
  - dual-stack sockets (`::` binds both families);
  - `poll`, non-blocking I/O, `SO_*` options, `sendmsg`/`recvmsg`.
- **Devices.** `net/netdev.c` holds the interfaces (`eth0`, `lo`) and
  implements the Linux `SIOC*` ioctls, so BusyBox `ifconfig`, `route` and
  `ip` work. Two private ioctls drive DHCP and DNS from `/sbin/netcfg`.
  It also provides `/proc/net/{dev,route,if_inet6,tcp,udp,...}`.
- **Addressing.** IPv4 from DHCP or static configuration; IPv6 SLAAC and
  link-local addresses.
- **Drivers.** virtio-net (QEMU) and GENET v5 (Pi 4).
- **Services.** Dropbear SSH, BusyBox `telnetd`, `httpd` and `ntpd`, and
  `syslogd`.

## Devices

The console is PL011 (or the Pi mini-UART) behind a TTY layer with a line
discipline, sessions, controlling terminals and job control. Pseudo-terminals
(`/dev/ptmx`, `/dev/pts/N`) serve SSH and telnet logins.

The Pi 4 drivers talk to the VideoCore firmware through the mailbox
property interface (`drivers/firmware/rpi_firmware.c`) for clocks, power
domains, the board serial and MAC address, temperature, the framebuffer and
the VL805 USB firmware reload. Userspace can reach the same interface
through `/dev/vcio`.

USB:
- PCIe host bridges: generic ECAM on QEMU, BCM2711 on the Pi.
- xHCI host controller driver.
- USB core with hubs.
- HID keyboards and mice, exposed as evdev devices; a USB keyboard also
  types into the console.
- Mass storage, as `/dev/sdX`.

[DRIVERS.md](DRIVERS.md) lists every driver with its test status.

## Userspace and services

- **libc and tools.** Userspace is musl 1.2.5 with BusyBox 1.36.1 and
  Dropbear 2024.86, all built from the pinned sources in `third_party/` by
  `toolchains/userspace/build.sh`. `oluxos-cc` is the compiler wrapper for
  more programs.
- **init** (`user/prog/init`) reads `/etc/init.conf`, with these entry
  types:
  - `sysinit`: run once at boot;
  - `once`: start once;
  - `respawn`: restart with back-off; exit status 78 means "not applicable
    here" and stops the restarts;
  - `console`: a login shell on the console;
  - `shutdown`: run at power-off or reboot.

  init reaps orphans, feeds `/dev/watchdog` (unless `init.watchdog=0`), and
  on power-off or reboot runs the shutdown entries, stops services, syncs
  and unmounts.
- **OluxOS tools**:
  - `netcfg`: interface configuration and DHCP;
  - `gpio`: GPIO lines;
  - `rpi-info`: firmware queries, and the tryboot flag;
  - `olux-update`: A/B updates;
  - `olux-verify`: Ed25519 signature checks;
  - `olux-latency`: timer and IPC latency;
  - `olux-selftest`: the kernel self-test suite.

## Reliability and lifecycle

- **Fault containment.** A crashing process gets a signal, and the rest of
  the system continues. A crashing service is restarted by init.
  - When memory runs out, an allocating system call fails with `ENOMEM`.
  - A page fault that cannot be satisfied kills only the faulting process,
    and the kernel logs it.
  - There is no per-user process limit yet (`RLIMIT_NPROC` is accepted but
    not enforced).
- **Panics.** A kernel panic prints registers and a symbolised backtrace,
  stops the other CPUs and reboots after `panic=` seconds (default 10).
- **pstore** (`kernel/pstore.c`). The kernel log is mirrored into a RAM
  region that survives a warm reset: the DT `ramoops` node, or the top
  64 KiB of the first bank, mapped non-cacheable.
  - The next boot reports how the previous one ended (reboot, power-off,
    panic, watchdog, or an unexpected reset).
  - The previous boot's log is in `/proc/last_kmsg`.
- **Watchdogs.** The BCM2835 PM watchdog, or a soft watchdog on QEMU, is
  exposed as the Linux-compatible `/dev/watchdog`. The kernel keeps the
  hardware alive until the user-set deadline, and init feeds it. The reset
  reason is reported at boot.
- **`/proc/sysrq-trigger`** (Linux-compatible):
  - `c`: crash;
  - `b`: reboot now;
  - `o`: power off;
  - `s`: emergency sync;
  - `t`: list every thread, its state, where it sleeps and the timer
    queues. This is the first thing to run on a hung system.
- **A/B updates** on the Pi 4, using the firmware's `autoboot.txt`
  and `tryboot`:
  - `olux-update` verifies a signed bundle (Ed25519) and installs it into
    the inactive boot slot;
  - it then trial-boots that slot once;
  - it confirms the slot after it has stayed up, and a failed trial falls
    back to the old slot.

  See [OPERATIONS.md](OPERATIONS.md).
- **Thermal.** On the Pi, `rpi-thermal` caps the ARM clock above the trip
  point (default 80 °C) and reports under-voltage and throttling flags from
  the firmware.

<a id="design-decisions"></a>
## Design decisions and deviations from the plan

The original plan ([PRODUCTION_READINESS_PLAN.md](PRODUCTION_READINESS_PLAN.md))
proposed a capability microkernel with every driver and the network stack in
userspace. The implementation chose a different split, for these reasons:

- **Linux ABI instead of a custom one.** Unmodified musl, BusyBox and
  Dropbear run as-is. This removed the need for a custom libc and made the
  standard tools the test suite.
- **Drivers in the kernel.** Interrupt latency, DMA and cache maintenance
  are simpler and faster there. Isolation effort went to the components
  that parse untrusted data from storage, the filesystem servers, which run
  in userspace, are restartable, and are reached over the same channel
  mechanism that userspace drivers would use.
- **lwIP in the kernel, not in a network server.** The socket API needs
  tight integration with `poll`, signals and blocking semantics. A
  userspace stack would have needed a much richer channel protocol first.
  lwIP runs under its own lock and is driven by one kernel thread, so it can
  be moved out later without changing the socket ABI.
- **A big kernel lock.** It kept SMP bring-up correct while the kernel grew.
  User code runs fully in parallel. Kernel-heavy workloads serialise, and
  splitting the lock is the next scalability step.
