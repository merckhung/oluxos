#include "src/scene/boot_script.h"

#include <algorithm>

namespace bootviz::scene {
namespace {

constexpr uint32_t kCyan = 0xFF5CD6FF;
constexpr uint32_t kAmber = 0xFFFFB547;
constexpr uint32_t kViolet = 0xFFB18CFF;
constexpr uint32_t kGreen = 0xFF6BE38A;
constexpr uint32_t kRose = 0xFFFF7A9A;
constexpr uint32_t kSky = 0xFF7FB2FF;

using P = Part;
using C = CpuState;

std::vector<Stage> MakeStages() {
  std::vector<Stage> s;

  s.push_back(Stage{
      .name = "Power-on",
      .title = "Power-on: the VideoCore boot ROM",
      .actor = "VideoCore VI VPU (the ARM cores are held in reset)",
      .where = "BCM2711 mask ROM",
      .steps = {"5 V is applied. Only the VideoCore GPU's control processor (VPU) runs; "
                "the four Cortex-A72 cores stay in reset.",
                "The VPU executes the boot ROM inside the SoC.",
                "The ROM reads the second-stage bootloader from the SPI EEPROM "
                "(or recovery.bin from the SD card, to reflash it)."},
      .duration = 5.f,
      .accent = kAmber,
      .active = {P::kRom, P::kVpu, P::kEeprom},
      .flows = {{P::kEeprom, P::kVpu, "bootloader", 0.25f, 0.95f, kAmber}},
      .cpus = {C::kReset, C::kReset, C::kReset, C::kReset},
      .console_note = "(silent: the firmware stages print only with uart_2ndstage=1)",
      .showcase = 0.62f,
  });

  s.push_back(Stage{
      .name = "Bootloader",
      .title = "EEPROM bootloader picks a boot slot",
      .actor = "VideoCore VPU running the EEPROM bootloader",
      .where = "scripts/mksdcard.sh (SD layout), olux-update (autoboot.txt)",
      .steps = {"Brings up clocks and the LPDDR4 SDRAM, then follows BOOT_ORDER: SD card first.",
                "Reads autoboot.txt from partition 1 (OLUXAUTO): tryboot_a_b=1, "
                "boot_partition=2, and [tryboot] boot_partition=3.",
                "Normal boots use slot A. After `olux-update try` the firmware's one-shot "
                "tryboot flag boots slot B once; a crash there falls back to A.",
                "Loads the VideoCore firmware start4.elf + fixup4.dat from the chosen slot."},
      .duration = 7.f,
      .accent = kAmber,
      .active = {P::kVpu, P::kEmmc2, P::kSdAuto, P::kSdBootA, P::kRam},
      .flows = {{P::kSdAuto, P::kVpu, "autoboot.txt", 0.10f, 0.45f, kAmber},
                {P::kSdBootA, P::kVpu, "start4.elf", 0.50f, 0.95f, kAmber}},
      .cpus = {C::kReset, C::kReset, C::kReset, C::kReset},
      .sd_slot = 2,
      .console_note = "(silent: the firmware stages print only with uart_2ndstage=1)",
      .showcase = 0.72f,
  });

  s.push_back(Stage{
      .name = "Firmware",
      .title = "start4.elf loads OluxOS and releases the ARM cores",
      .actor = "VideoCore firmware (start4.elf)",
      .where = "config.txt, cmdline.txt (scripts/mkrpi4.sh)",
      .steps = {"Reads config.txt: arm_64bit=1, kernel=kernel8.img, initramfs ... followkernel, "
                "enable_uart=1, dtoverlay=disable-bt, dtparam=watchdog/i2c_arm/spi.",
                "Loads bcm2711-rpi-4-b.dtb, applies the overlays and dtparams, and fills in "
                "/memory (the ARM/GPU split, gpu_mem=64) and /chosen: bootargs from "
                "cmdline.txt (console=ttyAMA0 ... olux.slot=a) and the initrd range.",
                "Copies kernel8.img and initramfs.cpio into SDRAM.",
                "Releases the ARM side: core 0 jumps to the kernel with x0 = the DTB address; "
                "cores 1-3 wait in the arm stub's spin table."},
      .duration = 8.f,
      .accent = kViolet,
      .active = {P::kVpu, P::kSdBootA, P::kEmmc2, P::kRam, P::kCpu0},
      .flows = {{P::kSdBootA, P::kRam, "kernel8.img", 0.05f, 0.40f, kViolet},
                {P::kSdBootA, P::kRam, "initramfs.cpio", 0.25f, 0.60f, kViolet},
                {P::kVpu, P::kRam, "device tree", 0.45f, 0.75f, kViolet},
                {P::kVpu, P::kCpu0, "release core 0", 0.80f, 1.00f, kGreen}},
      .cpus = {C::kReset, C::kReset, C::kReset, C::kReset},
      .sd_slot = 2,
      .console_note = "(silent: the firmware stages print only with uart_2ndstage=1)",
      .showcase = 0.55f,
  });

  s.push_back(Stage{
      .name = "head.S",
      .title = "Kernel entry: arch/arm64/head.S",
      .actor = "Cortex-A72 core 0, MMU off, entered at EL2",
      .where = "arch/arm64/head.S: primary_entry",
      .steps = {"The Image header says \"little-endian, 4 KiB pages, place anywhere\"; the "
                "code runs position independent until the MMU is on.",
                "Drops from EL2 to EL1, letting EL1 use the generic timer (CNTHCTL_EL2).",
                "Builds early page tables: the kernel image at 0xffffffff80000000 and the DTB "
                "in a fixmap window; turns on the MMU and caches.",
                "Calls start_kernel(dtb_pa, load_pa)."},
      .duration = 6.f,
      .accent = kCyan,
      .active = {P::kCpu0, P::kRam},
      .flows = {{P::kRam, P::kCpu0, "page tables", 0.30f, 0.80f, kCyan, 0.6f, -1}},
      .cpus = {C::kRun, C::kSpin, C::kSpin, C::kSpin},
      .console_note = "(nothing yet: the UART is set up in start_kernel)",
      .showcase = 0.62f,
  });

  s.push_back(Stage{
      .name = "start_kernel",
      .title = "start_kernel(): memory, interrupts, time, console",
      .actor = "Core 0 at EL1, MMU and caches on",
      .where = "kernel/main.c: start_kernel",
      .steps = {"Parses the device tree, starts the PL011 early console and prints the banner "
                "and the command line.",
                "memblock takes the RAM banks from /memory and reserves the kernel, the DTB, "
                "the initramfs, the spin table and 64 KiB for pstore (the crash log that "
                "survives a reset).",
                "Maps all RAM, starts the buddy page allocator and kmalloc, and checks pstore "
                "for how the previous boot ended.",
                "Probes the first driver levels: interrupt controller (GIC-400), timer (ARM "
                "generic timer), consoles (PL011, mini UART); creates the init thread."},
      .duration = 8.f,
      .accent = kCyan,
      .active = {P::kCpu0, P::kRam, P::kGic, P::kTimer, P::kUart, P::kSerial},
      .flows = {{P::kCpu0, P::kUart, "printk", 0.05f, 1.0f, kCyan, 0.62f, -1},
                {P::kUart, P::kSerial, "115200 8N1", 0.10f, 1.0f, kCyan},
                {P::kCpu0, P::kGic, "GICD/GICC", 0.60f, 0.85f, kSky, 0.45f, -1},
                {P::kTimer, P::kCpu0, "tick", 0.75f, 1.0f, kSky, 0.2f, 1}},
      .cpus = {C::kRun, C::kSpin, C::kSpin, C::kSpin},
      .log = {{0.05f, "[    0.000000] OluxOS 0.3.0 (00c94e8) AArch64"},
              {0.08f, "[    0.000000] Machine: Raspberry Pi 4 Model B"},
              {0.11f, "[    0.000000] Kernel loaded at 0x200000, DTB at 0x8400000, running at EL1"},
              {0.14f, "[    0.000000] Command line: console=ttyAMA0 olux.slot=a"},
              {0.20f, "[    0.000000]   memory:   [0x000000000000-0x00003bffffff] 960 MiB"},
              {0.23f, "[    0.000000]   reserved: [0x000000200000-0x0000002f3fff]"},
              {0.26f, "[    0.000000]   reserved: [0x00003bff0000-0x00003bffffff]"},
              {0.36f, "[    0.000000] zone DMA   : 242138 pages free"},
              {0.42f, "[    0.000000] pstore: 64 KiB at 0x3bff0000, boot #1"},
              {0.48f, "[    0.000000] Memory: 960 MiB total, 945 MiB free"},
              {0.62f, "[    0.000000] GICv2: 224 interrupts"},
              {0.76f, "[    0.201526] arch_timer: 62.50 MHz, virtual timer IRQ 27"},
              {0.86f, "[    0.208650] ttyAMA0: PL011 at serial@7e201000, IRQ 153 (console)"},
              {0.92f, "[    0.212083] ttyS0: BCM2835 mini UART"}},
      .showcase = 0.72f,
  });

  s.push_back(Stage{
      .name = "kernel_init",
      .title = "kernel_init(): drivers, SMP, root filesystem",
      .actor = "The init kernel thread (core 0), then all four cores",
      .where = "kernel/init.c: kernel_init",
      .steps = {"Probes the firmware level (the VideoCore mailbox: board, clocks, temperature), "
                "the bus level (PCIe root complex for the VL805 USB 3 controller) and the "
                "devices: GPIO, SPI, I2C, the PM watchdog, the SD card and its four "
                "partitions, the framebuffer, GENET Ethernet.",
                "Releases cores 1-3 from the spin table; all four now schedule threads.",
                "Unpacks the initramfs into the root tmpfs, mounts /dev, /proc, /tmp and /run, "
                "runs the initcalls (rpi-thermal, soft watchdog).",
                "Executes /sbin/init as process 1."},
      .duration = 9.f,
      .accent = kGreen,
      .active = {P::kCpu0, P::kMailbox, P::kVpu, P::kGpio, P::kPm, P::kEmmc2, P::kSdAuto,
                 P::kSdBootA, P::kSdBootB, P::kSdData, P::kPcie, P::kVl805, P::kGenet, P::kPhy,
                 P::kUart, P::kSerial, P::kRam},
      .flows = {{P::kCpu0, P::kMailbox, "property tags", 0.02f, 0.25f, kGreen},
                {P::kMailbox, P::kVpu, "board, clocks", 0.05f, 0.28f, kGreen},
                {P::kCpu0, P::kGpio, "pinmux", 0.15f, 0.35f, kSky, 0.75f, 1},
                {P::kEmmc2, P::kSdData, "partition table", 0.30f, 0.50f, kSky},
                {P::kCpu0, P::kPcie, "link up", 0.20f, 0.45f, kSky, 0.35f, -1},
                {P::kPcie, P::kVl805, "xHCI", 0.30f, 0.50f, kSky},
                {P::kGenet, P::kPhy, "MDIO", 0.35f, 0.55f, kSky},
                {P::kCpu0, P::kCpu1, "release", 0.58f, 0.70f, kGreen},
                {P::kCpu0, P::kCpu2, "release", 0.60f, 0.72f, kGreen},
                {P::kCpu0, P::kCpu3, "release", 0.62f, 0.74f, kGreen},
                {P::kRam, P::kCpu0, "unpack initramfs", 0.74f, 0.92f, kCyan, 0.6f, -1},
                {P::kUart, P::kSerial, "", 0.0f, 1.0f, kCyan}},
      .cpus = {C::kRun, C::kSpin, C::kSpin, C::kSpin},
      .log = {{0.04f, "[    0.224331] rpi-fw: firmware 0x548e1, Raspberry Pi 4 Model B rev 1.5 (2048 MiB), ARM 700 MHz, 25.0 C"},
              {0.12f, "[    0.235342] gpio: BCM2711 controller, 58 lines"},
              {0.17f, "[    0.243981] spi0: BCM2835 SPI at spi@7e204000 (spidev0.0, spidev0.1)"},
              {0.21f, "[    0.251413] i2c-1: BSC I2C at i2c@7e804000, 100 kHz"},
              {0.25f, "[    0.253511] bcm2835-pm: last reset: power-on (RSTS 0x1000)"},
              {0.31f, "[    0.263334] mmc: SDSC card 'QEMU!', 512 MiB, 4-bit, 50000 kHz"},
              {0.35f, "[    0.273373]   mmcblk0p1: start 2048, 16 MiB"},
              {0.37f, "[    0.275205]   mmcblk0p2: start 34816, 128 MiB"},
              {0.39f, "[    0.275284]   mmcblk0p3: start 296960, 128 MiB"},
              {0.41f, "[    0.275356]   mmcblk0p4: start 559104, 239 MiB"},
              {0.47f, "[    0.277424] fb0: 640x480, 32 bpp, pitch 2560, 2400 KiB at 0x3c100000"},
              {0.70f, "[    0.747312] smp: 4 CPU(s) online"},
              {0.82f, "[    0.790480] initramfs: unpacked 208 entries"},
              {0.87f, "[    0.797803] rpi-thermal: 25.0'C, ARM clock 700-700 MHz, trip point 80.0'C"},
              {0.90f, "[    0.798792] watchdog: softdog registered (default timeout 60 s)"},
              {0.95f, "[    0.800583] Freeing boot memory: 941 MiB free, 4 CPU(s) online"}},
      .showcase = 0.40f,
  });

  s.push_back(Stage{
      .name = "/sbin/init",
      .title = "/sbin/init: supervised services",
      .actor = "Process 1 and the services it starts (user space)",
      .where = "user/prog/init, rootfs/etc/init.conf",
      .steps = {"Runs /etc/init.d/rcS (hostname, DHCP on eth0) and arms the watchdog, which "
                "it then feeds.",
                "Starts and supervises the services in /etc/init.conf, restarting any that "
                "exit: mount-sd boot and mount-sd data (the FAT servers), syslogd, "
                "sshd-run (Dropbear), ntp-client.",
                "The filesystem servers run in user space: fatfsd serves slot A "
                "(mmcblk0p2) at /boot and partition 4 at /data over IPC channels.",
                "`olux-update confirm` makes a trial slot the default once it has stayed "
                "up for 60 seconds."},
      .duration = 8.f,
      .accent = kRose,
      .active = {P::kCpu0, P::kCpu1, P::kCpu2, P::kCpu3, P::kPm, P::kEmmc2, P::kSdBootA,
                 P::kSdData, P::kGenet, P::kPhy, P::kUart, P::kSerial},
      .flows = {{P::kCpu1, P::kPm, "watchdog feed", 0.10f, 1.0f, kRose, 0.10f, 1},
                {P::kSdData, P::kCpu2, "fatfsd → /data", 0.30f, 0.70f, kRose, 0.45f, 1},
                {P::kSdBootA, P::kCpu3, "fatfsd → /boot", 0.50f, 0.90f, kRose, 0.93f, 1},
                {P::kPhy, P::kGenet, "DHCP", 0.20f, 0.60f, kSky},
                {P::kUart, P::kSerial, "", 0.0f, 1.0f, kCyan}},
      .cpus = {C::kRun, C::kRun, C::kRun, C::kRun},
      .sd_slot = 2,
      .log = {{0.05f, "[    0.817282] init: OluxOS init starting"},
              {0.12f, "[    0.820128] init: watchdog armed"},
              {0.50f, "[    7.100437] fatfsd: /dev/mmcblk0p4: FAT32, 481910 clusters of 512 bytes, 481908 free"},
              {0.60f, "[    7.261462] ufs: mounted /dev/mmcblk0p4 on /data (rw)"},
              {0.78f, "[    9.445457] fatfsd: /dev/mmcblk0p2: FAT32, 258078 clusters of 512 bytes, 245166 free"},
              {0.88f, "[    9.653620] ufs: mounted /dev/mmcblk0p2 on /boot (rw)"}},
      .showcase = 0.62f,
  });

  s.push_back(Stage{
      .name = "Shell",
      .title = "Up and running",
      .actor = "Four cores scheduling user processes",
      .where = "docs/OPERATIONS.md",
      .steps = {"A root shell on the serial console (a USB keyboard types into it too); SSH "
                "with a key from /boot/authorized_keys.",
                "`cat /proc/last_kmsg` shows the previous boot's log if it ended in a panic "
                "or a watchdog reset.",
                "`olux-update install` writes a signed update into slot B; `olux-update try` "
                "boots it once through the firmware's tryboot flag."},
      .duration = 6.f,
      .accent = kGreen,
      .active = {P::kCpu0, P::kCpu1, P::kCpu2, P::kCpu3, P::kUart, P::kSerial, P::kRam},
      .flows = {{P::kSerial, P::kUart, "keystrokes", 0.30f, 0.70f, kGreen},
                {P::kUart, P::kSerial, "", 0.0f, 1.0f, kCyan}},
      .cpus = {C::kRun, C::kIdle, C::kRun, C::kIdle},
      .sd_slot = 2,
      .log = {{0.10f, ""},
              {0.12f, "Welcome to OluxOS (AArch64). Type 'help' for shell builtins, 'ps' for processes."},
              {0.16f, ""},
              {0.30f, "root@oluxos:~# uname -a"},
              {0.42f, "OluxOS oluxos 0.3.0 #1 SMP aarch64"},
              {0.55f, "root@oluxos:~# cat /proc/mounts | grep userfs"},
              {0.66f, "/dev/mmcblk0p4 /data userfs rw 0 0"},
              {0.70f, "/dev/mmcblk0p2 /boot userfs rw 0 0"},
              {0.80f, "root@oluxos:~# _"}},
      .showcase = 0.97f,
  });
  return s;
}

std::vector<MemRegion> MakeMemoryMap() {
  // Addresses from the recorded boot (QEMU raspi4b, 2 GiB board, gpu_mem=64);
  // the Pi firmware may place the kernel and initramfs elsewhere.
  return {
      {0x00000000, 0x1000, "spin table / arm stub", 0xFF8A94A6, 2, 0.85f},
      {0x00200000, 0xF4000, "kernel8.img (Image)", 0xFFB18CFF, 2, 0.20f},
      {0x08000000, 0x3A0000, "initramfs.cpio", 0xFF9F7BFF, 2, 0.45f},
      {0x08400000, 0x21000, "device tree (DTB)", 0xFFC9A8FF, 2, 0.65f},
      {0x3BFF0000, 0x10000, "pstore (crash log)", 0xFFFF7A9A, 4, 0.40f},
      {0x3C000000, 0x4000000, "VideoCore (gpu_mem=64)", 0xFFFFB547, 1, 0.20f},
      {0x00000000, 0x3C000000, "ARM RAM: 960 MiB, linear map", 0xFF5CD6FF, 4, 0.20f},
  };
}

}  // namespace

const std::vector<Stage>& BootStages() {
  static const std::vector<Stage> stages = MakeStages();
  return stages;
}

const std::vector<MemRegion>& MemoryMap() {
  static const std::vector<MemRegion> map = MakeMemoryMap();
  return map;
}

float TotalDuration() {
  float t = 0.f;
  for (const Stage& s : BootStages()) t += s.duration;
  return t;
}

Cursor CursorAt(float seconds) {
  const auto& stages = BootStages();
  float t = std::max(0.f, seconds);
  for (int i = 0; i < static_cast<int>(stages.size()); ++i) {
    if (t < stages[i].duration) return {i, t / stages[i].duration};
    t -= stages[i].duration;
  }
  return {static_cast<int>(stages.size()) - 1, 1.f};
}

float SecondsAt(Cursor c) {
  const auto& stages = BootStages();
  float t = 0.f;
  for (int i = 0; i < c.stage && i < static_cast<int>(stages.size()); ++i) t += stages[i].duration;
  if (c.stage < static_cast<int>(stages.size())) t += c.progress * stages[c.stage].duration;
  return t;
}

std::vector<std::string> ConsoleUpTo(Cursor c) {
  std::vector<std::string> out;
  const auto& stages = BootStages();
  for (int i = 0; i <= c.stage && i < static_cast<int>(stages.size()); ++i) {
    for (const LogLine& l : stages[i].log) {
      if (i < c.stage || l.at <= c.progress) out.push_back(l.text);
    }
  }
  return out;
}

}  // namespace bootviz::scene
