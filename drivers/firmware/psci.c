/* ARM Power State Coordination Interface: CPU_ON, SYSTEM_OFF, SYSTEM_RESET. */
#include <olux/device.h>
#include <olux/fdt.h>
#include <olux/kernel.h>
#include <olux/psci.h>
#include <olux/reboot.h>

#define PSCI_0_2_FN_PSCI_VERSION 0x84000000
#define PSCI_0_2_FN_CPU_OFF 0x84000002
#define PSCI_0_2_FN64_CPU_ON 0xc4000003
#define PSCI_0_2_FN_SYSTEM_OFF 0x84000008
#define PSCI_0_2_FN_SYSTEM_RESET 0x84000009

static bool use_hvc;
static bool available;
static u32 fn_cpu_on = PSCI_0_2_FN64_CPU_ON;

static long psci_call(u64 fn, u64 a0, u64 a1, u64 a2) {
  register u64 x0 __asm__("x0") = fn;
  register u64 x1 __asm__("x1") = a0;
  register u64 x2 __asm__("x2") = a1;
  register u64 x3 __asm__("x3") = a2;
  if (use_hvc)
    __asm__ volatile("hvc #0" : "+r"(x0) : "r"(x1), "r"(x2), "r"(x3) : "x4", "x5", "x6", "x7", "memory");
  else
    __asm__ volatile("smc #0" : "+r"(x0) : "r"(x1), "r"(x2), "r"(x3) : "x4", "x5", "x6", "x7", "memory");
  return (long)x0;
}

bool psci_available(void) { return available; }

void psci_cpu_off(void) {
  if (available) psci_call(PSCI_0_2_FN_CPU_OFF, 0, 0, 0);
}

int psci_cpu_on(u64 mpidr, phys_addr_t entry) {
  if (!available) return -ENODEV;
  long r = psci_call(fn_cpu_on, mpidr, entry, 0);
  return r == 0 ? 0 : r == -4 /* ALREADY_ON */ ? -EBUSY : -EIO;
}

static void psci_restart(void) { psci_call(PSCI_0_2_FN_SYSTEM_RESET, 0, 0, 0); }
static void psci_poweroff(void) { psci_call(PSCI_0_2_FN_SYSTEM_OFF, 0, 0, 0); }

static const struct reboot_ops psci_reboot = {.name = "psci", .restart = psci_restart, .poweroff = psci_poweroff};

static int psci_probe(int node) {
  const char *m = fdt_getprop_str(node, "method");
  if (!m) return -EINVAL;
  use_hvc = !strcmp(m, "hvc");
  available = true;
  u32 v;
  if (fdt_is_compatible(node, "arm,psci") && fdt_getprop_u32(node, "cpu_on", &v)) fn_cpu_on = v;
  bool v02 = fdt_is_compatible(node, "arm,psci-0.2") || fdt_is_compatible(node, "arm,psci-1.0");
  if (v02) {
    long ver = psci_call(PSCI_0_2_FN_PSCI_VERSION, 0, 0, 0);
    pr_info("psci: v%ld.%ld via %s\n", (ver >> 16) & 0xffff, ver & 0xffff, m);
    register_reboot_ops(&psci_reboot);
  }
  return 0;
}

DT_DRIVER(psci, DRV_FIRMWARE, psci_probe, "arm,psci-1.0", "arm,psci-0.2", "arm,psci");

/* PSCI is needed for SMP before the firmware level is probed; probe early. */
void psci_early_init(void);
void psci_early_init(void) {
  int n = fdt_find_compatible(-1, "arm,psci-1.0");
  if (n < 0) n = fdt_find_compatible(-1, "arm,psci-0.2");
  if (n < 0) n = fdt_find_compatible(-1, "arm,psci");
  if (n >= 0 && !available) psci_probe(n);
}
