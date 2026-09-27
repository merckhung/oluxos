/*
 * Raspberry Pi thermal and power supervision. A kernel thread reads the
 * SoC temperature from the firmware every two seconds; above the trip
 * point (default 80 'C, below the firmware's own 85 'C hard limit) it caps
 * the ARM clock at its minimum, and lifts the cap again 5 'C lower. It
 * also reports changes in the firmware's throttling flags (under-voltage,
 * frequency capping), which point at a weak power supply.
 *
 *   rpi_thermal.trip=<millidegrees>   trip point (0 disables the cap)
 */
#include <olux/device.h>
#include <olux/kernel.h>
#include <olux/rpi_firmware.h>
#include <olux/sched.h>
#include <olux/string.h>
#include <olux/time.h>

#define TAG_GET_TEMPERATURE 0x00030006
#define TAG_GET_MAX_CLOCK 0x00030004
#define TAG_GET_MIN_CLOCK 0x00030007
#define TAG_GET_THROTTLED 0x00030046
#define TAG_SET_CLOCK_RATE 0x00038002
#define HYSTERESIS 5000
#define POLL_NS (2 * NSEC_PER_SEC)

static u32 trip = 80000, arm_min, arm_max;
static bool hot, capped;

static int read_temp(u32 *mc) {
  u32 v[2] = {0, 0}; /* sensor 0 */
  if (rpi_fw_property(TAG_GET_TEMPERATURE, v, 4, 8) < 0) return -EIO;
  *mc = v[1];
  return 0;
}

static int set_arm_clock(u32 hz) {
  u32 v[3] = {RPI_CLK_ARM, hz, 0};
  return rpi_fw_property(TAG_SET_CLOCK_RATE, v, 12, 8) < 0 ? -EIO : 0;
}

static void report_throttled(u32 now, u32 before) {
  static const char *const what[4] = {"under-voltage", "ARM frequency capped", "throttled", "soft temperature limit"};
  for (int b = 0; b < 4; b++)
    if ((now ^ before) & (1u << b)) {
      if (now & (1u << b))
        pr_warn("rpi-thermal: firmware reports %s%s\n", what[b], b == 0 ? " (check the power supply)" : "");
      else
        pr_info("rpi-thermal: %s cleared\n", what[b]);
    }
}

static int thermald(void *arg) {
  (void)arg;
  u32 flags = 0;
  for (;;) {
    u32 t, f;
    if (read_temp(&t) == 0 && trip) {
      if (!hot && t >= trip) {
        hot = true;
        bool can_cap = arm_min && arm_max > arm_min;
        capped = can_cap && set_arm_clock(arm_min) == 0;
        pr_warn("rpi-thermal: %u.%u'C >= %u.%u'C trip point: %s\n", t / 1000, t % 1000 / 100, trip / 1000,
                trip % 1000 / 100, capped ? "ARM clock capped" : "ARM clock cannot be lowered further");
      } else if (hot && t + HYSTERESIS <= trip) {
        hot = false;
        if (capped && set_arm_clock(arm_max) == 0) capped = false;
        pr_info("rpi-thermal: %u.%u'C: below the trip point again%s\n", t / 1000, t % 1000 / 100,
                capped ? "" : ", full ARM clock");
      }
    }
    if (rpi_fw_get_u32(TAG_GET_THROTTLED, 0, &f) == 0) {
      report_throttled(f & 0xf, flags);
      flags = f & 0xf;
    }
    sleep_ns(POLL_NS);
  }
  return 0;
}

static int rpi_thermal_init(void) {
  if (!rpi_fw_available()) return 0;
  char val[16];
  if (cmdline_get("rpi_thermal.trip", val, sizeof(val))) trip = (u32)strtol(val, NULL, 10);
  u32 t;
  if (read_temp(&t)) return 0; /* no sensor */
  rpi_fw_get_u32(TAG_GET_MIN_CLOCK, RPI_CLK_ARM, &arm_min);
  rpi_fw_get_u32(TAG_GET_MAX_CLOCK, RPI_CLK_ARM, &arm_max);
  pr_info("rpi-thermal: %u.%u'C, ARM clock %u-%u MHz, trip point %u.%u'C\n", t / 1000, t % 1000 / 100,
          arm_min / 1000000, arm_max / 1000000, trip / 1000, trip % 1000 / 100);
  struct thread *th = kthread_create(thermald, NULL, "rpi-thermal");
  if (th) sched_add_new(th);
  return 0;
}
late_initcall(rpi_thermal_init);
