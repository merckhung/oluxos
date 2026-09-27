# Security model and threat model

This document says what OluxOS protects, against whom, how, and where the
limits are. It describes the current code. Items marked **gap** are known
weaknesses and are tracked in the plan.

## Assets

1. **Integrity of the running system**: the kernel, init and the services.
2. **Integrity of what the device boots next**: the boot slots and the
   update path.
3. **Data in `/data`** and the SSH host key.
4. **Availability**: the device keeps doing its job, or recovers by itself.

## Attackers considered

| Attacker | Capability | In scope |
|----------|------------|----------|
| Network attacker | Reaches the device's IP addresses | Yes |
| Malicious or compromised update source | Offers crafted update bundles | Yes |
| Buggy or compromised local process | Runs code as some uid on the device | Yes: the kernel must contain it |
| Malicious storage media | A crafted FAT or ext4 image, or a USB drive | Partly: the parsers run in userspace servers |
| Physical attacker | Holds the board or SD card | **No**: no secure boot or encryption (see gaps) |

## Mechanisms

### Kernel and process isolation

- **Address spaces.** Each process has its own address space, tagged with an
  ASID. The kernel is mapped only in TTBR1, with text read-only and
  executable, rodata read-only, and data and bss non-executable (W^X). User
  mappings are never executable by the kernel (PXN).
- **User pointers.** They are dereferenced only with the unprivileged
  `LDTR`/`STTR` instructions, so a system call cannot be tricked into
  touching kernel memory or a read-only user page. The self-tests
  `kernel_ptr_rejected` and `readonly_text_protected` check this.
- **Faults.** A fault in user mode signals the offending process only.
  A kernel fault panics and reboots rather than continuing in an unknown
  state.
- **Stack protection.** Kernel stacks have guard pages, the kernel is built
  with `-fstack-protector-strong`, and user programs are built with the
  same flag.
- **Randomness.** User stacks and `mmap` areas are randomised (ASLR).
  `AT_RANDOM`, `getrandom` and `/dev/urandom` come from a ChaCha20 CRNG
  seeded from the hardware RNG (RNG200 or virtio-rng) and timer jitter.
- **Permissions.** uid, gid and supplementary groups are checked for file
  access, signals and the privileged system calls (mount, reboot, time,
  raw sockets, and similar). Device nodes that expose hardware (`vcio`,
  `gpiochip0`, `i2c-*`, `spidev*`, `watchdog`) are `0600 root`.

### Services

- **Filesystem servers.** FAT and ext4 images are parsed by `fatfsd` and
  `ext4fsd` in userspace. A crafted image can crash the server, not the
  kernel, and init restarts it. The kernel side of the protocol validates
  every reply.
- **IPC identity.** Messages carry a sender pid, uid and gid stamped by the
  kernel, so a server can make access decisions without trusting the
  client. File descriptors passed in messages act as capabilities.
- **SSH.** Dropbear allows **key-only** root login. No password
  authentication is configured, and there are no default credentials.
  The host key is generated on the device on first boot.
- **Other services.** No other network services listen by default. Telnet
  and HTTP are available in BusyBox but are not started. The NTP client
  makes outbound connections only.

### Updates

- **Signature.** An update bundle's manifest is signed with Ed25519. The
  verifier (`olux-verify`) is a small standalone implementation, tested
  against RFC 8032 vectors and OpenSSL. It rejects non-canonical
  signatures.
- **Hashes and file list.** Every installed file is covered by a SHA-256
  hash in the signed manifest. Files not named in the manifest are never
  installed, and paths with `..` or a leading `/` are refused.
- **Isolation from the running system.** The update goes to the inactive
  slot, and the running slot is never modified. A slot that does not stay
  up is not confirmed, and the firmware falls back to the old slot.
- **Public key.** It is part of the signed image (`/etc/olux/update.pub`),
  so rotating keys is itself an update.

### Availability

- The hardware watchdog is fed by init, and a hang resets the board. A
  panic reboots.
- pstore keeps the log of the failed boot for diagnosis.
- Supervised services restart with back-off.
- The thermal governor caps the CPU clock before the firmware's hard limit.

## Known gaps

| Gap | Impact | Mitigation or plan |
|-----|--------|--------------------|
| **No secure boot.** The Pi 4 firmware loads whatever is on the SD card | A physical attacker can replace the kernel or the update key | Pi 4 "secure boot" with a customer key in OTP (rpi-eeprom signed boot) can sign the boot slot; not integrated yet |
| **No storage encryption** | Data on the card is readable by anyone who holds it | Encrypt application data at the application level |
| **Services run as root** | A compromised service, such as a filesystem server or sshd, has full control | Run servers under dedicated uids and hand them only the device fds they need (the channel model supports this) |
| **No per-user resource limits.** `RLIMIT_NPROC` and memory limits are not enforced | A local process can exhaust memory or processes (denial of service) | The watchdog recovers from a hang, but a slow exhaustion is not stopped |
| **Big kernel lock** | A process spinning in expensive system calls slows the others | |
| **No PAN, PAC, BTI or KASLR.** The Cortex-A72 lacks PAC and BTI, and PAN is not enabled | Fewer layers against kernel exploits | `LDTR`/`STTR` user access provides the main PAN guarantee. KASLR is optional future work |
| **The development update key** is generated when `UPDATE_KEY` is not set | Images built with it accept bundles signed by anyone with the build tree | Always build products with `UPDATE_KEY` pointing at an offline key; see [OPERATIONS.md](OPERATIONS.md) |
| **The drivers are in the kernel** | A driver bug is a kernel bug | Drivers validate device-provided lengths and indexes. USB descriptors and network frames are the main untrusted inputs, and they are bounds-checked |
| **No kernel fuzzing in CI** beyond the DTB parser | Parser bugs can go unnoticed | Fuzz the userfs protocol handling, USB descriptors and the network paths |

## Reporting

Security issues: contact the repository owner privately rather than opening
a public issue.
