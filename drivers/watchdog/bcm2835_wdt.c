/*
 * BCM2711/BCM2835 power manager: watchdog, restart, power-off (the firmware
 * halts when the "partition 63" boot code is set) and the last reset reason.
 */
#include <olux/device.h>
#include <olux/kernel.h>
#include <olux/reboot.h>
#include <olux/rpi_firmware.h>
#include <olux/time.h>
#include <olux/watchdog.h>

#define PM_RSTC 0x1c
#define PM_RSTS 0x20
#define PM_WDOG 0x24
#define PM_PASSWORD 0x5a000000u
#define PM_WDOG_TIME_MASK 0x000fffffu
#define PM_RSTC_WRCFG_CLR 0xffffffcfu
#define PM_RSTC_WRCFG_FULL_RESET 0x00000020u
#define PM_RSTC_RESET 0x00000102u
#define PM_RSTS_HADWRH 0x00000040u
#define PM_RSTS_PARTITION_HALT 0x00000555u /* partition 63 */
#define TICKS_PER_SEC 65536u

static u8 *pm;

static int wdt_start(struct watchdog_device *wd, unsigned timeout) {
  writel(PM_PASSWORD | ((timeout * TICKS_PER_SEC) & PM_WDOG_TIME_MASK), pm + PM_WDOG);
  u32 cur = readl(pm + PM_RSTC);
  writel(PM_PASSWORD | (cur & PM_RSTC_WRCFG_CLR) | PM_RSTC_WRCFG_FULL_RESET, pm + PM_RSTC);
  wd->priv = (void *)(uintptr_t)timeout;
  return 0;
}

static int wdt_ping(struct watchdog_device *wd) {
  unsigned timeout = (unsigned)(uintptr_t)wd->priv;
  writel(PM_PASSWORD | ((timeout * TICKS_PER_SEC) & PM_WDOG_TIME_MASK), pm + PM_WDOG);
  return 0;
}

static int wdt_stop(struct watchdog_device *wd) {
  writel(PM_PASSWORD | PM_RSTC_RESET, pm + PM_RSTC);
  return 0;
}

static struct watchdog_device bcm_wdt = {
    .name = "bcm2835-pm-wdt",
    .max_hw_timeout = PM_WDOG_TIME_MASK / TICKS_PER_SEC, /* 15 s */
    .start = wdt_start,
    .stop = wdt_stop,
    .ping = wdt_ping,
};

static void pm_restart(void) {
  writel(PM_PASSWORD | 10, pm + PM_WDOG); /* ~150 us */
  u32 cur = readl(pm + PM_RSTC);
  writel(PM_PASSWORD | (cur & PM_RSTC_WRCFG_CLR) | PM_RSTC_WRCFG_FULL_RESET, pm + PM_RSTC);
  udelay(10000);
}

static void pm_poweroff(void) {
  /* the firmware reads the partition field and halts instead of booting */
  u32 v = readl(pm + PM_RSTS);
  writel(PM_PASSWORD | v | PM_RSTS_PARTITION_HALT, pm + PM_RSTS);
  pm_restart();
}

static const struct reboot_ops pm_reboot_ops = {.name = "bcm2835-pm", .restart = pm_restart, .poweroff = pm_poweroff};

static int pm_probe(int node) {
  pm = dt_ioremap(node, 0, NULL);
  if (!pm) return -ENOMEM;
  u32 rsts = readl(pm + PM_RSTS);
  if (rsts & PM_RSTS_HADWRH)
    last_reset_reason = "watchdog";
  else
    last_reset_reason = "power-on";
  pr_info("bcm2835-pm: last reset: %s (RSTS %#x)\n", last_reset_reason, rsts);
  register_reboot_ops(&pm_reboot_ops);
  /* QEMU's model resets as soon as the watchdog is armed (it does not
   * count), and reports serial 0; do not offer the watchdog there. */
  u32 serial[2] = {0, 0};
  if (rpi_fw_property(RPI_FW_GET_BOARD_SERIAL, serial, 0, 8) >= 0 && !serial[0] && !serial[1]) {
    pr_info("bcm2835-pm: emulated board (serial 0): hardware watchdog not used\n");
    return 0;
  }
  watchdog_register(&bcm_wdt);
  return 0;
}

DT_DRIVER(bcm2835_pm, DRV_DEVICE, pm_probe, "brcm,bcm2835-pm-wdt", "brcm,bcm2711-pm", "brcm,bcm2835-pm");
