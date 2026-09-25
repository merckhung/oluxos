# Writing and testing OluxOS drivers

Drivers are built into the kernel and bind to hardware in one of three ways:

- device-tree nodes, by `compatible` string;
- PCI functions, by vendor, device and class;
- USB interfaces, by class, subclass and protocol.

This guide covers the model, the kernel services drivers use, a worked
example, and how to test. It ends with the driver list and its test status.

## Binding

### Device tree

```c
static int foo_probe(int node) {
  u8 *regs = dt_ioremap(node, 0, NULL);        /* first "reg" entry, device memory */
  if (!regs) return -ENOMEM;
  u32 irq, flags;
  if (fdt_get_irq(node, 0, &irq, &flags)) return -ENODEV;
  ...
  return 0;                                    /* 0 claims the node */
}
DT_DRIVER(foo, DRV_DEVICE, foo_probe, "vendor,foo-v2", "vendor,foo");
```

`start_kernel` and `kernel_init` visit every *available* node
(`status = "okay"` or no status) once per level, in this order:

| Level | Probed | Use it for |
|-------|--------|------------|
| `DRV_IRQCHIP` | Early, one CPU, no scheduler | Interrupt controllers |
| `DRV_TIMER` | Early | Clock sources and events |
| `DRV_CONSOLE` | Early | Consoles (the log is replayed to them) |
| `DRV_FIRMWARE` | From the init thread, may sleep | Firmware interfaces (PSCI, the Pi mailbox) that later drivers call |
| `DRV_BUS` | Init thread | Buses that create children (PCIe host bridges) |
| `DRV_DEVICE` | Init thread | Everything else |

A probe runs once per matching node. A non-zero return leaves the node
unclaimed, so a later driver may still take it. Work that does not belong
to a node goes in an initcall (`late_initcall(fn)` runs after all device
probes), for example `drivers/firmware/rpi_thermal.c`.

Useful helpers (`include/olux/device.h`, `include/olux/fdt.h`):

| Helper | Returns |
|--------|---------|
| `fdt_getprop`, `fdt_getprop_u32`, `fdt_getprop_str` | Node properties |
| `fdt_get_reg`, `fdt_get_irq` | Translated `reg` and interrupt specifiers |
| `dt_dma_addr(node, pa)` | Bus address for DMA, from `dma-ranges` (the Pi's VideoCore view differs from the CPU's) |
| `dt_alias_id(node, "i2c")` | N for `/aliases/i2cN`; used to name devices (`/dev/i2c-1`) |
| `fdt_node_by_phandle`, `fdt_find_compatible` | Other nodes |
| `gpio_apply_pinctrl(node)` | Applies the node's `pinctrl-0` (BCM2711 pin functions and pulls) |

### PCI

```c
static int bar_probe(struct pci_dev *d) {
  u8 *regs = ioremap(d->bar[0], d->bar_size[0]);
  pci_set_master(d);
  request_irq(d->irq, bar_irq, priv, "bar");   /* INTx, routed via interrupt-map */
  ...
}
PCI_DRIVER(bar, .name = "bar", .vendor = PCI_ANY, .device = PCI_ANY,
           .class = 0x0c0330, .class_mask = 0xffffff, .probe = bar_probe);
```

A host bridge driver (ECAM, `pcie-brcmstb`) calls `pci_host_scan`. This
enumerates the buses, assigns memory BARs from the bridge's `ranges`,
routes INTx through `interrupt-map` and binds the PCI drivers. Use
`pci_bus_addr(dev, pa)` for DMA addresses.

### USB

```c
static int baz_probe(struct usb_interface *intf) {
  /* intf->ep[] lists the endpoints; intf->dev->ops has control/bulk/interrupt transfers */
}
USB_DRIVER(baz, .name = "baz", .cls = 8, .subcls = USB_ANY, .proto = USB_ANY,
           .probe = baz_probe, .disconnect = baz_disconnect);
```

The USB core (`drivers/usb/usb.c`) reads the descriptors, selects the first
configuration, has the host controller configure the endpoints, and offers
each interface to the drivers. Transfers are synchronous (`control`, `bulk`)
or a resubmitting interrupt IN poll (`intr_start`, whose callback runs in
interrupt context). After `disconnect`, every transfer on the device fails
with `-ENODEV`. The `usb_device` structure stays allocated, so a driver may
keep pointers to it.

## Kernel services

| Need | API | Notes |
|------|-----|-------|
| MMIO | `readl`/`writel` (and 8/16/64-bit forms) | Device-nGnRE mappings; the accessors include the needed barriers |
| Interrupts | `request_irq(irq, fn, arg, name)` | Handlers run with IRQs off on the receiving CPU; keep them short, then wake a thread |
| Sleeping | `sleep_ns`, `msleep`, wait queues (`wait_event_interruptible[_timeout]`, `wake_up`), `struct completion` | Only in thread context (probe, syscalls, kernel threads) |
| Delays | `udelay`, `mdelay` | Busy-wait; fine in probe for short hardware settle times |
| Locks | `spinlock_t` (`spin_lock_irqsave`) for data shared with IRQ handlers; `struct mutex` for sleeping sections | System calls also hold the big kernel lock, which is released while sleeping |
| Timers | `ktimer_init`/`ktimer_start`/`ktimer_cancel` | One-shot, absolute monotonic ns; callback in interrupt context |
| Threads | `kthread_create(fn, arg, name)` then `sched_add_new(t)` | Kernel threads never return; loop with a sleep |
| Memory | `kmalloc`/`kzalloc`/`kfree`, `get_free_pages`, `vmalloc` | `GFP_*` zone flags matter only for DMA |
| DMA | `dma_alloc_coherent(size, &pa, GFP_DMA or GFP_DMA32)` | Uncached, so no cache maintenance is needed; translate `pa` with `dt_dma_addr`/`pci_bus_addr` |
| Character devices | `register_chrdev(MKDEV(maj, min), name, &fops, priv)` + `devfs_create(name, S_IFCHR \| mode, dev)` | Use Linux's major and ioctl numbers where Linux has them, so existing tools work |
| Block devices | `blkdev_register(bd, major, part_sep)` | Implement `ops->rw` (synchronous, may sleep) and optionally `ops->flush`; the buffer cache and partitions come for free |
| Network devices | `netdev_register(nd)` | `ops->xmit` queues a frame (no sleeping); the IRQ handler calls `netdev_schedule`; `ops->poll` hands frames to `netdev_rx` from the `netd` thread |
| Input devices | `input_register`, `input_event`, `input_sync` | Appear as `/dev/input/eventN` (evdev ABI) |
| Watchdogs | `watchdog_register` | The core provides `/dev/watchdog` and the kernel heartbeat |
| RTCs | `rtc_register(ops, priv)` | The core sets the system time at boot and provides `/dev/rtc0` |
| Reboot and power-off | `register_reboot_ops` | The most recently registered mechanism wins |
| Pi firmware | `rpi_fw_property`, `rpi_fw_get_u32`, `rpi_fw_clock_rate`, `rpi_fw_set_power` | Mailbox property tags, serialised and bounce-buffered for you |
| Logging | `pr_err`/`pr_warn`/`pr_info`/`pr_debug` | Prefix messages with the device name |

Conventions:

- **Validate everything a device or DMA descriptor tells you.** Lengths,
  indexes and counts come from hardware and may be wrong.
- **Always use timeouts** on hardware handshakes. On a timeout, log it and
  fail the probe or request with `-ETIMEDOUT` rather than spinning forever.
- **Reuse Linux ABIs** (ioctl numbers, structure layouts, device numbers)
  for user-visible interfaces. The Linux headers userspace compiles against
  are in `toolchains/userspace/linux-headers/`.
- **Keep the code in the house style**: one driver per file, a header
  comment that says what the hardware is and what the driver supports, and
  Linux's register names.

## Example: a minimal RTC driver

`drivers/rtc/pl031.c` shows the whole shape in 35 lines: probe maps the
registers, starts the counter, and registers with the RTC core. The core
provides `/dev/rtc0`, sets the time at boot and keeps the RTC updated when
the time is stepped.

## Testing

**Build.** Run `make`. Warnings are errors (`-Wall -Wextra -Werror`).

**QEMU `virt`.** Run `make run` for an interactive session, or
`tests/qemu/run_tests.py` for the suite:
- It adds virtio block, network and RNG devices, an xHCI controller with a
  keyboard, a mouse and a hub with a USB drive, and a monitor socket for
  hot-plug (`device_del`) and synthetic input (`sendkey`, `mouse_move`).
- Add a `t_<name>(c)` function and list it in `TESTS`. `c.run(cmd)`
  returns the output and exit status.

**QEMU `raspi4b`** (QEMU 9 or later; `scripts/ci/install-qemu9.sh` builds
it). Run `tests/qemu/run_tests.py --machine raspi4b --qemu qemu9-aarch64`.
It covers the mailbox, GPIO, SD card, I2C, SPI, framebuffer, watchdog and
thermal paths. The test DTB enables the I2C and SPI nodes as the firmware
would. QEMU does not model GENET, RNG200, PCIe or the thermal sensor
hardware, and disables those nodes.

**Self-tests.** `olux-selftest [name]` runs in-guest checks of kernel
interfaces (for example `spidev`, `framebuffer`, `pty`). Add a case for
every new user-visible interface.

**Diagnostics:**
- `echo t > /proc/sysrq-trigger` lists every thread with its kernel
  backtrace, plus the timer queues.
- `/proc/interrupts` shows the interrupt counts.
- `dmesg` shows probe messages.
- `/proc/last_kmsg` has the log from before the last reset.

## Driver list and test status

"Tested" means the automated QEMU suite exercises the driver. The Pi 4
column refers to QEMU `raspi4b`. **None of the drivers has been run on a
physical Raspberry Pi 4 in the development environment.** Drivers marked
*untested* follow the Linux driver's register sequences for the same
hardware but have never run. Treat them as bring-up candidates.

| Driver | File | Hardware | Status |
|--------|------|----------|--------|
| GICv2/GICv3 | `drivers/irqchip/gic.c` | QEMU virt, BCM2711 GIC-400 | Tested (virt gic2 and gic3; raspi4b) |
| ARM generic timer | `drivers/timer/arch_timer.c` | All | Tested |
| PL011 UART | `drivers/serial/pl011.c` | QEMU, Pi 4 console | Tested |
| BCM2835 mini-UART | `drivers/serial/bcm2835_aux.c` | Pi 4 alternative console | Not in the automated suite |
| PSCI | `drivers/firmware/psci.c` | QEMU (CPU_ON, reset, off) | Tested |
| Pi firmware mailbox, `/dev/vcio` | `drivers/firmware/rpi_firmware.c` | Pi 4 | Tested (raspi4b) |
| Pi thermal and power supervision | `drivers/firmware/rpi_thermal.c` | Pi 4 | Trip logic tested (raspi4b reports a constant 25 °C and fixed clocks); clock capping untested |
| virtio-mmio, -blk, -net, -rng | `drivers/virtio/*`, `drivers/net/virtio_net.c` | QEMU virt | Tested |
| PL031 RTC | `drivers/rtc/pl031.c` | QEMU virt | Tested |
| PCI core, ECAM host | `drivers/pci/pci.c` | QEMU virt | Tested |
| BCM2711 PCIe root complex | `drivers/pci/pcie_brcmstb.c` | Pi 4 (VL805 USB) | **Untested**: not modelled by QEMU |
| xHCI | `drivers/usb/xhci.c` | QEMU `qemu-xhci`, Pi 4 VL805 | Tested on QEMU; the VL805 path is untested |
| USB hub, HID, mass storage | `drivers/usb/{hub,hid,storage}.c` | Any | Tested (keyboard typing, mouse evdev, drive behind a hub, hot-unplug) |
| evdev | `drivers/input/evdev.c` | Any | Tested |
| BCM2711 GPIO | `drivers/gpio/bcm2711_gpio.c` | Pi 4 | Tested (raspi4b) |
| EMMC2 SDHCI | `drivers/mmc/sdhci.c` | Pi 4 SD card | Tested (raspi4b), PIO mode |
| BCM2835 PM watchdog, reset and power-off | `drivers/watchdog/bcm2835_wdt.c` | Pi 4 | Reset reason and power-off tested; the hardware watchdog is skipped on QEMU (board serial 0) |
| Watchdog core and softdog | `drivers/watchdog/watchdog.c` | All | Tested (expiry resets the machine) |
| RNG200 | `drivers/char/bcm2711_rng.c` | Pi 4 | **Untested**: QEMU disables the node |
| Framebuffer | `drivers/video/rpi_fb.c` | Pi 4 (mailbox) | Tested (raspi4b self-test) |
| BSC I2C | `drivers/i2c/bcm2835_i2c.c` | Pi 4 | Tested (raspi4b, bus scan and NAK path) |
| SPI | `drivers/spi/bcm2835_spi.c` | Pi 4 | Tested (raspi4b self-test) |
| GENET v5 + BCM54213PE PHY | `drivers/net/bcmgenet.c` | Pi 4 Ethernet | **Untested**: QEMU does not model GENET |
| TTY, PTY, `/dev/mem`, random | `drivers/char/*` | All | Tested |
