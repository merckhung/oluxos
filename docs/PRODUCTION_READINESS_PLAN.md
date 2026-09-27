# OluxOS Production-Readiness Assessment & Plan

**Scope:** ARM64 on Raspberry Pi 4B (BCM2711) and QEMU `virt` / `raspi4b`.
**Baseline:** branch head `1b94da1` (2026-09). The code was reviewed statically. No AArch64 cross toolchain or QEMU was available during review, so nothing was built or booted.

> Sections 1–7 below are the original assessment of that baseline and the
> plan made from it. **Section 0 records what has since been implemented**
> (version 0.3.0), how it deviates from the plan, and what is still open.

---

## 0. Implementation status (0.3.0, 2026-09-25)

The prototype was replaced by a new AArch64 kernel with a Linux-compatible
ABI. Everything below is verified by the automated QEMU suites:

- `virt`: 1 and 4 CPUs, GICv2 and GICv3;
- `raspi4b`;
- about 30 scenarios plus 51 in-guest kernel self-tests on each machine;
- a soak test.

**No part has been run on a physical Raspberry Pi 4 yet.** Every exit
criterion that says "on a Pi 4" is therefore still open. The first task is a
hardware bring-up pass with the checklist in
[PORTING.md](PORTING.md#hardware-bring-up-checklist).

| Phase | Status | Delivered | Open |
|-------|--------|-----------|------|
| 0 Build, test, scope | **Done** (with deviations) | Other architectures moved to `legacy/`. Userspace (musl, BusyBox, Dropbear) and disk images are built from pinned sources. Cross-toolchain build script. `-Wall -Wextra -Werror`. CI runs the build, unit tests with ASan and UBSan, a DTB fuzzer, the QEMU suites on `virt` and `raspi4b`, cppcheck and clang-format | Bazel wraps Make rather than using per-subsystem `cc_*` rules with a registered toolchain. **No LICENSE: the owner must choose one.** No CONTRIBUTING.md |
| 1 Correctness and safety | **Done** | Per-thread FP/SIMD and TLS state. Preemption. IRQ-safe locking. `LDTR`/`STTR` user access with an exception table. Faults signal the process, and a kernel fault panics with a symbolised backtrace and reboots. Correct page tables and TLB maintenance. Extensions moved to numbers ≥ 1000. Position-independent image (fixes the Pi `kernel_address` problem) | Real-hardware boot not yet verified |
| 2 Kernel architecture | **Done** | TTBR1 higher-half kernel with ASIDs. Device-tree discovery. Buddy allocator with DMA zones, slab allocator. VMAs, COW, `mmap`. Separate processes and threads, futexes. O(1) priority scheduler with an RT class. SMP via PSCI and the spin table. GICv2/v3. Clocks, NTP discipline, RTC. printk, panic, WARN | A **big kernel lock** serialises system calls: splitting it is the main scalability item. No 24-hour stress run on record, but the soak test exists (`make soak`) |
| 3 IPC and security model | **Partial, by design** | Channels with kernel-attested sender identity and descriptor passing (capabilities as descriptors). FAT and ext4 servers in userspace, supervised, restartable, and re-attaching to their mounts. `AT_RANDOM` and the CRNG seeded from hardware RNGs. W^X. uid/gid enforcement | **Deviation:** drivers and the network stack live in the kernel, and there is no userspace MMIO or IRQ delivery and no manifest-based capability grants. See the rationale in [ARCHITECTURE.md](ARCHITECTURE.md#design-decisions). Services still run as root |
| 4 POSIX and userland | **Done** | VFS, tmpfs, devtmpfs, procfs, TTY and PTY with job control, signals, pipes, `poll`, `eventfd`, `memfd`. Upstream musl, with BusyBox and Dropbear unmodified | LTP or libc-test conformance runs; the in-tree self-tests cover the core semantics instead |
| 5 Storage | **Mostly done** | Block layer with a write-back cache, MBR and GPT. virtio-blk, EMMC2 SD (PIO), USB mass storage. FAT12/16/32 read-write with LFN and repair after an unclean shutdown; ext2/3/4 read-only. Persistence across power-off is tested | **Deviation:** the data partition is FAT with repair, not littlefs or a journal, so a power cut can lose the last writes but not the volume. No randomised power-cut test campaign yet. No SD DMA (ADMA2) or high-speed modes |
| 6 Drivers and networking | **Mostly done** | Pi mailbox, GPIO (with edge waits), PL011 baud setup, watchdog and reset reason, I2C, SPI, framebuffer, RNG200, GENET v5, PCIe (brcmstb) + xHCI + hubs + HID + mass storage, thermal. QEMU: virtio-blk/net/rng, PL031, PSCI, ECAM PCI. lwIP TCP/IP (IPv4/IPv6, DHCP, DNS), BSD sockets, SSH, NTP, syslog, telnet and HTTP | **GENET, RNG200 and PCIe/VL805 are untested** (QEMU does not model them). No PWM, DMA engine, virtio-console or virtio-gpu. **Deviation:** lwIP runs in the kernel, not as a server |
| 7 Robustness and lifecycle | **Mostly done** | Watchdog fed by init. pstore crash log that survives reset, with the reset reason. A/B updates via `tryboot` with Ed25519-signed bundles and automatic fallback. `/proc/sysrq-trigger` (including thread backtraces). `olux-latency`. Soak test. Thermal governor. WFI idle. Stack protector in the kernel and userland. Threat model ([SECURITY.md](SECURITY.md)) | 7-day soak on a Pi 4. Scheduler and IRQ trace buffer. GDB stub (QEMU `-s` via `make debug` only). cpufreq beyond thermal capping. KASLR. PAC/BTI (not on Cortex-A72). FORTIFY (musl has none). Secure boot |
| 8 Docs and release | **Partly done** | Architecture, driver guide with test status, syscall ABI, porting, operations and security docs. Semantic versioning with a changelog. SBOM ([THIRD_PARTY.md](../THIRD_PARTY.md)). CI artifacts for the SD image | Signed release artifacts and a release process. A hardware-in-the-loop rig |

Bugs found and fixed along the way that are worth remembering:
- Races in the userfs mount handshake and in signals (ignored-but-blocked
  signals were dropped).
- `O_NOCTTY` was handled too early, which broke SSH PTYs.
- An AF_UNIX use-after-free.
- A `nanosleep` overflow that could make a thread sleep for about 146
  years; found by the latency test and diagnosed with sysrq-t.
- TCP connections were reset on close under memory pressure.
- Stack waiters could be used uninitialised (found by cppcheck).

### Next steps, in priority order

1. **Hardware bring-up on a Pi 4.** Serial boot, SD card, GENET, PCIe and
   the VL805 with USB, RNG200, watchdog reset, the pstore survival rate,
   `tryboot` A/B.
2. **Choose and add a LICENSE.** It blocks any redistribution.
3. Run services under dedicated uids and enforce `RLIMIT_NPROC` and memory
   limits.
4. Split the big kernel lock (VFS and sockets first).
5. A power-cut campaign on the FAT data partition; consider littlefs or a
   journal for `/data`.
6. A hardware-in-the-loop CI rig and signed release artifacts.

---

## 1. Executive summary

OluxOS is an impressive **bring-up prototype**. On QEMU it boots to EL1, turns on the MMU and runs userspace servers over synchronous IPC. It can also load an unmodified static glibc BusyBox and get an `ash` prompt. That is a solid demo of the microkernel idea.

It is **not yet a professional-grade embedded OS**, and it is not close. Measured against what a product team would need on a Pi 4 (reliability, isolation, real storage, networking, field update, observability, reproducible builds, tests), it is roughly at **15–20 %**. The most important gaps:

| # | Gap | Why it matters |
|---|-----|----------------|
| 1 | **No preemption.** The timer IRQ is never enabled (`arch/arm64/kernel/krn.c:1795-1796`). | Any busy-looping process hangs the whole system. |
| 2 | **FP/SIMD registers and `TPIDR_EL0` are not saved per thread.** `switch.S` and `vector.S` save only the GPRs. | glibc uses NEON in memcpy/strlen and TPIDR for TLS. Once two processes run concurrently, they silently corrupt each other. |
| 3 | **Syscalls do not check user pointers.** `translate_user_va` walks page tables that also map all kernel RAM (`vmm.c:66`) and ignores the AP bits. | Any process can make `read()`/`write()` read or overwrite kernel memory. |
| 4 | **No isolation between servers.** `SYS_MAP_MMIO` needs no privilege. IPC senders identify themselves in the payload. The FS handle table is global. On RPi4 the MMIO grant flips AP bits in a page table shared by every process (`task.c:610-623`). | A single bad or malicious process owns the device. |
| 5 | **Memory model is a prototype.** RAM is hardcoded to about 128 MB (`krn.c:1783`), there is no DTB, and the D-cache is off (`boot.S:99-103`). There is no cache maintenance, `vmm_map` does no TLBI, and fork copies every page (no COW). | Slow, and it wastes 97 % of an 8 GB Pi. Latent coherency bugs will show up the moment caches are enabled. |
| 6 | **Faults are not contained.** Any EL0 fault that is not a syscall calls `exception_handler_dump`, which is `while(1)` (`krn.c:1531-1570`). | One segfault stops the device. |
| 7 | **Storage is read-only, from a RAM image.** There is no SD/eMMC driver. FAT32 support is root-directory-only, 8.3 names, with no writes. ext4 is read-only with no bounds checking. | You cannot persist data, logs or configuration, or update in the field. |
| 8 | **Almost no Pi 4 hardware support.** There is no GPIO, clock, watchdog, SD, Ethernet, USB, I2C or SPI driver, and no SMP. `pl011_init` is empty. | It depends on firmware to set everything up and offers only a serial console. |
| 9 | **The build is not reproducible.** `testdata/fat.img` is referenced but not in the repo, a prebuilt `user/busybox` is committed, and `toolchains/oluxos-clang` has a bash syntax error. | A clean checkout cannot build `run_qemu_arm64` or `rpi4_boot_zip`. |
| 10 | **Zero tests, zero CI.** | Every change is a regression risk. |

On real Pi 4 hardware, the boot ZIP is likely broken as shipped. The kernel is linked at `0x40080000` (`arch/arm64/kernel.lds:5`), but the generated `config.txt` has no `kernel_address=0x40080000`, so the firmware loads `kernel8.img` at `0x80000`. The EL2→EL1 drop also never sets `CNTHCTL_EL2`, so EL1 physical-timer accesses may trap once the timer is enabled.

---

## 2. What exists today (ARM64 path)

| Area | Present | Maturity |
|------|---------|----------|
| Boot | EL3/EL2→EL1 drop, static identity page tables, vector table, FP enable | Bring-up |
| Memory | Single-range LIFO page allocator, K&R heap (≤4080-byte objects), 3-level 4 KB VMM, per-process TTBR0 | Prototype |
| Scheduling | Round-robin over a static table of 8 threads, 4 KB kernel stacks, cooperative | Prototype |
| IPC | Synchronous send/recv of raw structs, copied physically; addressed by fixed TIDs 1–4 | Prototype |
| Syscalls | About 30 Linux arm64 numbers (read, write, openat, getdents64, fstat, brk, mmap, clone, execve, wait4…) plus 7 custom calls that collide with Linux numbers | Demo-level |
| Drivers | PL011 (polled plus RX IRQ), GICv2 (minimal), ARM generic timer (unused), VideoCore mailbox, 640×480 framebuffer | Bring-up |
| Userspace | A single `shell.c` binary acting as the UART, ramdisk, FS and shell servers; tiny `ulib`; ELF loader; software GLES 1.1 and window manager | Demo |
| Filesystems | FAT32 (read-only, root only), ext4 (read-only, extents only) | Demo |
| Tooling | Bazel genrules that call host cross-compilers; a partial LLVM `OluxOS` triple patch | Fragile |
| Debug | `kdbger` UART protocol (has a buffer overflow), `print_hex` tracing | Ad hoc |

The other five architectures (IA32, x86_64, RISC-V 32/64, ARM32) each carry separate, partly duplicated kernels. They multiply maintenance cost without helping the Pi 4 goal.

---

## 3. Detailed gap analysis

Severity: **C** = critical (security or corruption), **H** = high (blocks the product), **M** = medium, **L** = low.

### 3.1 Boot & platform bring-up
- **H** No `kernel_address` in the generated `config.txt`, which conflicts with the `0x40080000` link address (`BUILD.bazel`, `rpi4_boot_zip`). Either link position-independently or emit `kernel_address=0x40080000`.
- **H** The DTB pointer in `x0` is overwritten at `boot.S:6`. There is no FDT parser, so RAM size, peripheral bases and IRQ numbers are all compile-time constants spread across 4+ files.
- **H** The D-cache (`SCTLR.C`) is never enabled. SCTLR is OR-ed rather than set to a known value. There is no `TLBI VMALLE1` before the MMU goes on.
- **H** The EL2 exit does not configure `CNTHCTL_EL2`/`CNTVOFF_EL2`. There is no stack per EL and no `SP_EL0` or `SPSel` hygiene.
- **M** Secondary cores are parked in `wfe` forever. There is no PSCI (QEMU) or spin-table (Pi 4) release, and no per-CPU data (`include/kernel/cpu.h` is RISC-V only).
- **M** Kernel and user share TTBR0 (the identity map). There is no higher-half kernel in TTBR1, so every address space has to carry kernel mappings.

### 3.2 Memory management
- **C** `translate_user_va` (`task.c:440`) accepts kernel addresses and ignores AP and UXN bits. `copy_to_user`, `copy_from_user` and IPC therefore write to any mapped memory, including kernel RAM and read-only user text.
- **C** The IPC copy translates only the first page of each buffer and then does a flat `CbMemCpy` (`task.c:491-513`, `566-576`). A buffer that crosses a page boundary corrupts an unrelated physical page.
- **H** `vmm_map` treats 1 GB and 2 MB block descriptors as table pointers, masks with `~0xFFF` instead of the output-address mask, silently overwrites and leaks existing leaves, and never issues TLBI. `vmm_unmap` leaks the page and does no TLBI either.
- **H** `vmm_dup_aspace` copies only `l1[0]`, and `vmm_free_aspace` frees every USER page. Shared mappings (MMIO, ramdisk, framebuffer) can be double-freed.
- **H** The PMM covers one hardcoded range up to `0x48000000`. It has no zones, no >4 GB support, no contiguous or aligned allocation (needed for DMA), no refcounts and no reserved regions (the DTB, the framebuffer and the mailbox buffer at `0x1000000` are unprotected).
- **H** `IntDisable`/`IntEnable` do not save and restore state. `pmm_alloc_page` unconditionally re-enables IRQs inside callers' critical sections, including `kmalloc` and syscall paths.
- **M** Fork copies the whole address space eagerly (no COW). `munmap` and `mprotect` are stubs that report success. `mmap` ignores `fd` and `flags` and uses a single global bump pointer shared by all processes (`task.c:741`).
- **M** The heap has no lock, caps objects at 4080 bytes and never returns pages.
- **M** Mappings set no PXN/UXN, so W^X is not enforced. Kernel RAM is mapped executable for EL1. There are no guard pages on kernel or user stacks.
- **M** MAIR has only Device-nGnRnE and Normal-WB. The framebuffer and DMA buffers need Normal-NC or Device-nGnRE.

### 3.3 Threads, scheduling & exceptions
- **C** No FP/SIMD context (`q0-q31`, `FPCR`, `FPSR`) and no `TPIDR_EL0` in the saved context.
- **C** A user fault hangs the whole kernel instead of killing the process (SIGSEGV semantics). An EL1 fault prints registers and spins, with no backtrace and no reset.
- **H** Preemption is disabled. The timer uses TVAL re-arming, so it drifts. There is no `nanosleep`, no `clock_gettime` and no timers.
- **H** `MAX_THREADS 8` is static, each kernel stack is 4 KB with no guard, and threads and processes are conflated. `clone(CLONE_VM|CLONE_THREAD)` is treated as fork.
- **M** `wait4` always reports exit status 0. There are no zombies, reparenting, exit codes or signals.
- **M** The scheduler is O(n) round-robin with no priorities, no real-time class and no idle accounting. It does a full `TLBI VMALLE1IS` on every switch, with no ASIDs.

### 3.4 IPC, isolation & security model
- **C** Any process can call `SYS_MAP_MMIO` or `SYS_SPAWN`. `SYS_SPAWN` hands a raw user pointer to `thread_create_userspace`, which reads it directly.
- **C** The server code is exploitable: the UART server writes out of bounds via `req.data[req.size]` (`shell.c:1017`), the ramdisk reads an unbounded sector (`shell.c:1051`), and the FS server does not clamp `count` (`shell.c:1140`).
- **H** Sender identity is self-declared. There are no endpoints or capabilities, no name service, no timeouts, no reply correlation, and no notification or IRQ-to-userspace path. The kernel's UART is hardwired into the kernel's own stdin path.
- **H** Custom syscalls 1–7 collide with Linux arm64 numbers (for example `setxattr` is 5). The ABIs must be separated.
- **M** `AT_RANDOM` is a constant (`task.c:415`, `loader.c:83`), so glibc stack canaries and pointer guards are predictable.

### 3.5 POSIX / userland
- **H** Missing: pipes, `dup`/`dup3`, signals (rt_sigaction is stubbed), `poll`/`select`, `clock_gettime`, `nanosleep`, `uname`, `getrandom`, `lseek` (partial), write support, `/dev`, and a controlling TTY with a line discipline. As a result ash pipelines, job control, `sleep`, `date` and most applets fail.
- **H** The committed `user/busybox` binary does not match `toolchains/BUILD_SUMMARY.md`, so its provenance is unverifiable. It is built against host glibc, which assumes a full Linux kernel.
- **M** `ulib` is a toy libc: no malloc, errno, stdio `FILE` or `%ld`. It has several unterminated `strncpy`s and trusts server-supplied sizes (`unistd.c:170`, `dirent.c:55`).

### 3.6 Storage & filesystems
- **H** There is no block driver: no EMMC2/SDHCI on the Pi 4 and no virtio-blk on QEMU. The root filesystem is a firmware-loaded RAM image at a fixed address (`0x48000000`, 34 MB).
- **H** FAT32 is read-only, root directory only, 8.3 names only, and assumes 512-byte sectors. ext4 is read-only, lacks 64-bit group descriptors, and does not bounds-check `rec_len`, `name_len` or `eh_entries`, so a corrupt image can hang or crash the FS server.
- **M** There is no block cache. Every sector is an IPC round trip. There is no VFS, no mount table and no power-loss-safe write path.

### 3.7 Device drivers (Pi 4 / QEMU)
- **H** The mailbox has no DSB, no cache maintenance, no bus-address aliasing (`|0xC0000000`), no timeout and no lock.
- **H** PL011 has no baud or clock setup and no GPIO14/15 ALT0 muxing, so it relies entirely on firmware.
- **H** The GICv2 init is incomplete: it never reads TYPER, never disables or clears or configures SPIs, and sets no group, priority mask or BPR. It relies on signed-shift UB, does unlocked read-modify-write, and mishandles spurious interrupt 1023.
- **H** Missing Pi 4 drivers: GPIO, clocks and power domains via mailbox, watchdog (`PM_RSTC`/`PM_WDOG`) and reboot, EMMC2, GENET Ethernet, PCIe plus VL805 xHCI (USB), I2C, SPI, thermal and RNG200.
- **M** Missing QEMU drivers: virtio-mmio (blk, net, console, rng, gpu), PL031 RTC, and PSCI `SYSTEM_OFF`/`SYSTEM_RESET`.
- **M** The framebuffer never queries pitch or pixel order, is mapped cacheable, and on RPi4 is unmapped whenever a user address space is active.

### 3.8 Build, test, process
- **H** `testdata/fat.img` is missing and no rule generates it. `user/busybox` is a committed prebuilt. The toolchain scripts are broken (`oluxos-clang:59` has a syntax error, and `oluxos-ld.py` rejects `-static`). There is no hermetic Bazel toolchain.
- **H** No unit tests, no QEMU integration tests, no CI and no static analysis. The ARM64 kernel compiles as a single genrule at `-std=gnu89` with no `-Werror`.
- **M** `krn.c` is a 1,800-line god file that mixes syscalls, the UART driver, termios and the ELF loader. Two ELF loaders are duplicated (`krn_execve` and `thread_create_userspace`). There are dozens of `#if 0` debug blocks. `clib.c` has broken `CbStrCat`, `CbStrCmp` and `CbStrCpy`.
- **L** No LICENSE file. The README links point to `file:///home/merck/...`.

---

## 4. Target definition ("professional grade")

The plan below aims at a concrete, testable definition. The target is a **secure, preemptive, SMP microkernel OS** with these properties:

1. It boots unmodified from the stock Pi 4 firmware and from QEMU `virt`, with hardware discovered from the DTB.
2. It isolates faults: a crashing driver or app never takes down the kernel, and critical services restart.
3. It persists data to SD (Pi 4) or virtio-blk (QEMU) with a power-loss-safe filesystem.
4. It offers a POSIX subset that runs a from-source musl BusyBox, including pipes, signals and time.
5. It provides a serial console, networking (GENET or virtio-net plus a TCP/IP stack), a watchdog, and A/B field updates.
6. It builds hermetically from a clean checkout, has CI on every commit, and passes unit, integration and fuzz tests.

---

## 5. Improvement plan

Each phase has deliverables and **exit criteria**. Effort is given in engineer-weeks (ew) for one experienced systems engineer. Phases 0–2 are strictly sequential. Phases 4–6 can overlap once Phase 3 is done.

### Phase 0: Make it buildable, testable and focused (≈2–3 ew)
1. **Decide the scope.** Move IA32, x86_64, RISC-V 32/64 and ARM32 to `legacy/` (or a branch) and drop them from default Bazel targets. Make ARM64 the only supported architecture until Phase 6.
2. **Reproducible images.**
   - Add a Bazel rule that builds `fat.img` or `ext4.img` from sources (`mkfs.vfat`/`mtools` or `mke2fs -d`).
   - Delete the committed `user/busybox` and build BusyBox from a pinned tarball with a checksum.
   - Remove the `.gitignore` entry that hides `testdata/`.
3. **Hermetic toolchain.**
   - Register an AArch64 `cc_toolchain` (LLVM or ARM GNU toolchain via `http_archive`) instead of genrules calling host `aarch64-linux-gnu-gcc`.
   - Replace the monolithic genrule with `cc_binary` or `cc_library` per subsystem.
   - Fix or delete `toolchains/oluxos-clang` and `oluxos-ld.py`.
4. **Compiler hygiene.**
   - Use `-std=gnu11 -Wall -Wextra -Werror -fno-common -fstack-protector-strong -mgeneral-regs-only` (kernel).
   - Add `.clang-format` enforcement and `clang-tidy`/`cppcheck` in CI.
5. **CI (GitHub Actions).** Build all ARM64 targets. Boot `virt` and `raspi4b` in QEMU under `pexpect`, and assert on the banner, a BusyBox prompt, and `ls`/`cat` results. Keep serial logs as artifacts.
6. **Host unit-test harness** (Bazel `cc_test` on the host) for `clib`, PMM, heap, the ELF parser and the FAT/ext4 parsers. Seed it with regression tests for the known `clib` bugs.
7. **Hygiene.** Add a LICENSE, fix the README links, and add `CONTRIBUTING.md` and a coding standard.

**Exit:** a clean clone can run `bazel test //...` and `bazel run //:run_qemu_arm64`, and both are green in CI.

### Phase 1: Critical correctness and safety fixes (≈3–4 ew)
Keep the current architecture, but make it correct. Each item gets a regression test.
1. **Save per-thread FP/SIMD and TLS state.**
   - Save and restore `q0-q31`, `FPCR`, `FPSR` and `TPIDR_EL0` in the thread context. Eager saving is fine to start; switch to lazy saving with `CPACR` traps later.
   - Also save `TPIDRRO_EL0`.
2. **Preemption.** Enable the timer PPI, re-arm it with `CVAL += period`, and set `CNTHCTL_EL2.EL1PC{T,EN}` in the EL2 exit. Make preemption from EL1 syscall paths safe (a preempt count, or keep kernel non-preemptible but enable IRQs).
3. **IRQ flags.** Replace `IntDisable`/`IntEnable` with `irq_save()`/`irq_restore(flags)` built on `DAIF`, and audit every caller (PMM, heap, IPC, scheduler).
4. **User-memory access.**
   - `translate_user_va` must reject addresses outside the user VA range and check AP[1] (EL0 access) plus AP[2] for writes. Everything must go through `copy_{to,from}_user` and `strncpy_from_user` with page-by-page translation.
   - Fix the page-crossing IPC copy.
5. **Fault containment.** An EL0 data or instruction abort, alignment fault or undefined instruction should kill the process (exit code 128+SIGSEGV or SIGILL) and log it. An EL1 fault should panic with a register dump, a frame-pointer backtrace and a symbol lookup, then reboot via watchdog or PSCI.
6. **VMM.**
   - Fix handling of block entries and the address masks.
   - Add TLBI by VA on unmap and remap, and refuse to overwrite an existing leaf.
   - Track page ownership so shared or MMIO pages are never freed.
   - Enable the D-cache and add `dc cvac`/`civac` helpers.
7. **Restrict privileged syscalls.** Gate `SYS_MAP_MMIO` and `SYS_SPAWN` to TIDs listed in a static boot manifest. This is an interim measure until the Phase 3 capabilities. Move the custom syscalls to a range that cannot collide with Linux (for example ≥ 0x1000) or to a separate `svc #1` immediate.
8. **Server hardening.** Bounds-check every IPC request in the UART, ramdisk and FS servers, and validate ELF headers in both loaders. Fuzz the FAT, ext4 and ELF parsers with libFuzzer on the host.
9. **Pi 4 boot fix.** Emit `kernel_address=0x40080000` (or relink to `0x80000`), and add a hardware smoke checklist.
10. **Delete dead code:** `#if 0` blocks, the duplicate ELF loader and the unused shell loop. Fix `clib`.

**Exit:** two CPU-bound processes and BusyBox interleave correctly under preemption. A user process that dereferences NULL is killed while the system keeps running. The fuzzers run for 1 hour without a crash. The kernel boots on real Pi 4 hardware to a shell.

### Phase 2: Kernel architecture for a real product (≈8–10 ew)
1. **Higher-half kernel.** Put the kernel in TTBR1 (`0xFFFF…`) and users in TTBR0 with ASIDs, so switches no longer need a global TLBI. Map kernel text RX, rodata R and data RW-XN, and turn on PAN, plus UAO where available.
2. **Device tree.** Keep `x0`, add a small FDT parser (or vendor `libfdt`), and discover memory (including RAM above 4 GB on 4 and 8 GB boards), the UART, GIC, timer IRQ and the `reserved-memory` nodes. Add a board abstraction layer (`board_rpi4.c`, `board_virt.c`) in place of `#if CONFIG_BOARD_RPI4`.
3. **Physical memory.**
   - Build a buddy allocator over multiple regions with a `struct page` array (refcount and flags).
   - Add a DMA zone (<1 GB for BCM2711 legacy DMA and <3 GB for EMMC2) and contiguous allocations.
   - Add a slab allocator for kernel objects, with guard pages and poisoning in debug builds.
4. **Virtual memory.** Add VMAs per address space, demand paging and zero-fill, COW `fork`, and real `mmap`, `munmap`, `mprotect` and `MAP_SHARED`. Also add `vfork`/`posix_spawn` fast paths, stack guard pages and ASLR (optional).
5. **Processes and threads.**
   - Split `Process` (address space, fd table, credentials, children) from `Thread` (context, kernel stack), with dynamic allocation and 16 KB kernel stacks plus guard pages.
   - Support `CLONE_THREAD`/`CLONE_VM`, futexes, `set_tid_address` and `exit_group`.
   - Add zombies, reparenting to init, and real exit status.
6. **Scheduler.** Add per-CPU runqueues with priorities (fixed-priority preemptive plus round-robin within a priority level, which suits embedded use), a real-time class, sleep queues driven by a timer wheel or hrtimer, and a tickless idle mode (optional).
7. **SMP.** Bring up secondary cores with PSCI `CPU_ON` (QEMU) or the spin table (Pi 4 armstub at `0xd8`–`0xf0`). Add per-CPU data via `TPIDR_EL1`, IPIs over GIC SGIs, ticket spinlocks with `irqsave` variants, and TLB shootdowns. Audit every global (PMM, heap, IPC, the UART ring buffer).
8. **GICv2 done properly.** Read TYPER, configure groups, priorities, targets and trigger types, handle the spurious ID, and provide an IRQ-routing API. Keep GICv3 in view for other boards.
9. **Time.** Add a monotonic clock and a realtime clock (PL031 on QEMU; NTP or RTC hat on Pi), plus `clock_gettime`, `nanosleep` and `timerfd` (optional).
10. **Kernel logging.** Add a `printk` ring buffer with levels, a proper `printf` (64-bit, `%p`, width), `panic()`, `BUG_ON` and `WARN_ON`.

**Exit:** all 4 cores run on Pi 4, and all detected RAM is usable. `stress`-style tests (fork bombs, mmap churn, threads with futexes) run for 24 hours in QEMU without leaks. `free` matches after the test.

### Phase 3: IPC and security model (≈4–6 ew)
1. **Endpoints and capabilities.** Processes hold per-process handle tables to kernel objects (endpoint, memory object, IRQ, MMIO region). A **kernel-attested badge** on every message replaces the self-declared `sender` field.
2. **Messages.**
   - Use a fixed register-based fast path (for example 8 words) plus an out-of-line buffer or shared-memory grants for bulk data (fs reads, framebuffer).
   - Add `call`/`reply_recv` semantics with reply capabilities, timeouts and async notifications.
3. **Userspace drivers.** Deliver IRQs to userspace through notification objects. Grant MMIO through capabilities, mapped per process at page granularity, never by editing shared tables. Add IOMMU-less DMA buffer grants.
4. **Name service and init.**
   - `init` reads a manifest that says which servers to start, which capabilities each receives, and the restart policy.
   - Remove the fixed TIDs.
   - Split `shell.c` into separate `uartd`, `blkd`, `vfsd` and `fatfs`/`ext4fs` binaries.
5. **Supervision.** `init` watches servers and restarts crashed drivers, and clients get `EIO` rather than hanging forever.
6. **Security baseline.**
   - Random `AT_RANDOM` from RNG200 on Pi 4 or virtio-rng on QEMU.
   - W^X everywhere, and uid/gid in the process struct.
   - Remove `kdbger` from release builds, or authenticate it.

**Exit:** killing `fatfs` produces an error in the client and an automatic restart, with the rest of the system unaffected. A process without an MMIO capability cannot touch the UART.

### Phase 4: POSIX layer and userland (≈5–7 ew)
1. **VFS server.** Add a mount table, path resolution with `..`, symlinks and cwd per process, an fd table with shared file descriptions (`dup`, `fork` inheritance), `O_APPEND`/`O_CREAT`/`O_TRUNC`, and `lseek`, `pread` and `pwrite`.
2. **Special filesystems:** devfs (`/dev/console`, `/dev/null`, `/dev/zero`, `/dev/urandom`, `/dev/ttyAMA0`, `/dev/mmcblk0`), tmpfs for `/tmp` and `/run`, and a minimal procfs (`/proc/self`, `meminfo`, `uptime`).
3. **TTY layer.** Add a line discipline (canonical mode, echo, signals from `^C`/`^Z`), sessions and process groups, and `TIOCSPGRP`.
4. **Signals.** Implement `rt_sigaction`, `sigprocmask`, `kill`, `tgkill`, signal frames with `rt_sigreturn`, and default actions. Deliver SIGCHLD, SIGSEGV, SIGINT and SIGPIPE.
5. **Pipes, poll and events:** `pipe2`, `dup3`, `poll`/`ppoll`, `select`, `eventfd` and `fcntl`.
6. **libc.** Port **musl** with an OluxOS syscall layer (or keep the Linux ABI and run upstream musl unmodified). Build BusyBox, and later other packages, from source with it, and retire `ulib`. Keep a syscall-coverage table in the docs.
7. **Conformance.** Run a subset of the Linux Test Project (LTP) or `libc-test` in QEMU CI, and track the pass rate.

**Exit:** `ash` pipelines (`ls | grep x > /tmp/f`), `sleep`, `^C`, `kill`, `date` and `top` work. The chosen LTP subset passes at ≥ 90 %.

### Phase 5: Storage and persistence (≈5–7 ew)
1. **Block layer.** Add a block-device server interface, a buffer cache (LRU, write-back with explicit sync), partition parsing (MBR and GPT), and a request queue.
2. **Drivers:** **virtio-blk** (QEMU `virt`) and **EMMC2 SDHCI** (Pi 4) with DMA (ADMA2), high-speed modes, card detect and error recovery. Keep the ramdisk as `initramfs`.
3. **Filesystems.**
   - FAT32 with full read/write, LFN, subdirectories and FSInfo; the Pi 4 boot partition needs this.
   - For data, either a robust log-structured or journaled filesystem (port **littlefs**, which is well tested and power-loss safe), or read-write ext4 with a journal.
   - Recommendation: ext4 read-only root plus a littlefs or ext4 data partition.
4. **Integrity.** Run fsck-on-boot for FAT, add power-cut tests in QEMU (kill QEMU during writes, then verify), and mount read-only on error.

**Exit:** the system boots from SD on a Pi 4 with a read-only root and a writable `/data`. 1,000 randomized power-cut cycles in QEMU produce no corruption.

### Phase 6: Platform drivers and networking (≈10–14 ew; can run in parallel after Phase 3)
**Pi 4 (BCM2711)**
- Mailbox v2: barriers, bus-address aliasing, cache maintenance, a lock, a timeout, and per-tag status checks. Build a firmware property API on it (clocks, power domains, temperature, board revision, MAC address, memory split).
- GPIO: function select, pulls, and edge IRQs through the GIC. PL011 with proper baud from `GET_CLOCK_RATE`, FIFOs, and TX/RX IRQs with ring buffers. The mini-UART is optional.
- **Watchdog** (`PM_WDOG`/`PM_RSTC`), with kernel and `init` petting it, plus reboot and poweroff.
- **GENET v5 Ethernet** with an RGMII PHY (BCM54213PE) and MDIO.
- **PCIe root complex plus VL805 xHCI:** the USB host stack, then HID keyboard/mouse and mass storage. This is the largest single item, at about 4–6 ew.
- I2C (BSC), SPI, PWM, RNG200 and the thermal sensor, then DMA engine and HDMI framebuffer improvements (pitch, virtual offset, double buffering).

**QEMU `virt`**
- virtio-mmio transport, then virtio-blk, virtio-net, virtio-rng, virtio-console and virtio-gpu (optional).
- PL031 RTC, and PSCI for poweroff, reset and `CPU_ON`.

**Networking**
- A network server that ports **lwIP** (the de-facto choice for embedded use), with IPv4 and IPv6, TCP/UDP, DHCP and DNS.
- A BSD sockets API in the POSIX layer (`socket`, `bind`, `connect`, `accept`, `send`, `recv`, `poll`).
- Services: `telnetd` or `dropbear` sshd, `ntpd`, and an HTTP server for testing.

**Exit:** on a Pi 4, DHCP gets an address, SSH login works, and a USB keyboard and a thumb drive work. The QEMU CI suite includes a networking test via user-mode networking.

### Phase 7: Embedded-grade robustness, observability and lifecycle (≈4–6 ew)
1. **Reliability.**
   - A hardware watchdog supervised by `init`, and a panic handler that stores the crash log in a reserved RAM region that survives reset, then reboots.
   - Report the reset reason at boot.
2. **Field update.** A/B boot partitions using Pi 4 `tryboot` and `autoboot.txt`, image signing (Ed25519) verified by the updater, rollback on boot failure, and version metadata.
3. **Observability.**
   - `dmesg`, per-process CPU and memory accounting, and a `/proc`-style stats server.
   - A trace buffer (scheduler, IPC, IRQ latency) and a GDB stub over UART or QEMU `-s` with symbolised crash dumps.
4. **Real-time characterisation.** Measure IRQ-to-thread latency and IPC round-trip time with `cyclictest`-style tools on the Pi 4, set budgets, and fail CI on regressions (in QEMU, relative numbers only).
5. **Power and thermal.** WFI idle, cpufreq through mailbox clock rates, and thermal throttling.
6. **Security hardening.** Stack protector and FORTIFY in userland, KASLR (optional), PAC/BTI where the core supports them (the Cortex-A72 does not, but QEMU `-cpu max` does), and a documented threat model.

**Exit:** a 7-day soak test on a Pi 4 (network traffic plus FS writes plus periodic driver kills) finishes with no hang and no leak, and every induced crash is recovered by watchdog or supervisor.

### Phase 8: Documentation and release engineering (ongoing, ≈1 ew per release)
- Architecture docs (boot flow, memory map, IPC, capability model), a driver-writing guide, a syscall ABI reference and a porting guide.
- Semantic versioning, a changelog, signed release artifacts (`sdcard.img.xz`, QEMU images), and an SBOM for the third-party components (musl, BusyBox, lwIP, littlefs).
- A hardware-in-the-loop rig: a Pi 4 with a USB-serial adapter, a relay for power cycling and network boot, running nightly tests.

---

## 6. Recommended first 10 pull requests

| # | PR | Phase | Size |
|---|----|-------|------|
| 1 | Generate `fat.img` from a Bazel rule; build BusyBox from a pinned source; remove the committed binary | 0 | M |
| 2 | QEMU `virt` and `raspi4b` boot smoke tests plus a GitHub Actions workflow | 0 | M |
| 3 | Emit `kernel_address=0x40080000` in `config.txt`; set `CNTHCTL_EL2`; invalidate TLBs before the MMU is enabled | 1 | S |
| 4 | Save and restore FP/SIMD, `FPCR`/`FPSR` and `TPIDR_EL0` per thread | 1 | S |
| 5 | `irq_save`/`irq_restore` everywhere; enable timer preemption with CVAL | 1 | M |
| 6 | Harden `translate_user_va` (user range plus AP checks); make user copies and IPC page-safe | 1 | M |
| 7 | Kill a faulting EL0 process instead of hanging; panic plus backtrace for EL1 faults | 1 | M |
| 8 | Fix `vmm_map` block handling, masks and TLBI; add ownership tracking for shared or MMIO pages | 1 | M |
| 9 | Gate `MAP_MMIO` and `SPAWN`; move custom syscalls out of the Linux number space; bounds-check the servers | 1 | M |
| 10 | Split `krn.c` into `syscall/`, `tty.c`, `exec.c`; delete dead code; fix `clib`; turn on `-Wextra -Werror` | 1 | M |

---

## 7. Effort and risk summary

| Phase | Effort (ew) | Key risk |
|-------|-------------|----------|
| 0 Build/CI/scope | 2–3 | Hermetic AArch64 Bazel toolchain setup |
| 1 Critical fixes | 3–4 | Enabling D-cache and preemption will surface hidden races |
| 2 Kernel architecture | 8–10 | Higher-half plus SMP is a deep refactor, so land it incrementally behind tests |
| 3 IPC/capabilities | 4–6 | API churn across every server |
| 4 POSIX/userland | 5–7 | Long tail of syscall semantics |
| 5 Storage | 5–7 | EMMC2 quirks on real silicon |
| 6 Drivers/network | 10–14 | PCIe/xHCI complexity; limited BCM2711 documentation (use the Linux and U-Boot drivers as reference) |
| 7 Robustness/lifecycle | 4–6 | Needs a hardware test rig |
| **Total** | **≈41–57 ew** | About 10–13 months for one engineer, or about 5–6 months for a team of 2–3 |

**Guiding principle:** do not add features until Phases 0 and 1 are green. Most of the current bugs are silent corruption, not crashes, and they will make every later feature look flaky.
