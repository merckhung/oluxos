/*
 * Kernel entry after head.S: bring up memory management, interrupts, time
 * and the console on the boot CPU, then continue in the init thread.
 */
#include <asm/memory.h>
#include <asm/pgtable.h>
#include <olux/device.h>
#include <olux/fdt.h>
#include <olux/irq.h>
#include <olux/kernel.h>
#include <olux/mm.h>
#include <olux/sched.h>
#include <olux/smp.h>
#include <olux/time.h>

void start_kernel(phys_addr_t dtb_pa, phys_addr_t load_pa);
void pl011_early_init(void);

extern char _text[], _end[];
struct cpu cpus[NR_CPUS];
int nr_cpus_possible = 1;
int nr_cpus_online = 1;
static phys_addr_t initrd_start, initrd_end;
static char cmdline[512];

const char *kernel_cmdline(void) { return cmdline; }

void initrd_get(phys_addr_t *start, phys_addr_t *end);
void initrd_get(phys_addr_t *start, phys_addr_t *end) {
  *start = initrd_start;
  *end = initrd_end;
}

static void add_memory(u64 base, u64 size, void *arg) { memblock_add(base, size); }
static void reserve_memory(u64 base, u64 size, void *arg) { memblock_reserve(base, size); }

static void setup_memory(phys_addr_t dtb_pa) {
  fdt_for_each_memory(add_memory, NULL);
  if (!memblock_memory_count()) panic("no memory described in the device tree");
  memblock_reserve(kernel_phys_start, kernel_phys_end - kernel_phys_start);
  memblock_reserve(dtb_pa, fdt_totalsize(fdt_blob()));
  u64 a, s;
  for (int i = 0; fdt_mem_rsv(i, &a, &s); i++) memblock_reserve(a, s);
  fdt_for_each_reserved(reserve_memory, NULL);
  if (fdt_initrd(&a, &s)) {
    initrd_start = a;
    initrd_end = s;
    memblock_reserve(a, s - a);
  }
}

void start_kernel(phys_addr_t dtb_pa, phys_addr_t load_pa) {
  kimage_voffset = (u64)_text - load_pa;
  kernel_phys_start = load_pa;
  kernel_phys_end = load_pa + ((u64)_end - (u64)_text);

  cpus[0].id = 0;
  cpus[0].mpidr = read_sysreg(mpidr_el1) & 0xff00ffffffUL;
  cpus[0].online = true;
  write_sysreg(tpidr_el1, &cpus[0]);

  /* The DTB is reachable through the fixmap window set up by head.S. */
  const void *dtb = (const void *)(FIXMAP_START + FIXMAP_DTB_OFFSET + (dtb_pa & (L2_SIZE - 1)));
  if (!dtb_pa || fdt_init(dtb) != 0) {
    for (;;) wfe(); /* nothing we can do without a device tree or console */
  }
  if ((dtb_pa & (L2_SIZE - 1)) + fdt_totalsize(dtb) > FIXMAP_DTB_SIZE) {
    for (;;) wfe();
  }
  pl011_early_init();

  pr_notice("OluxOS %s (%s) AArch64\n", OLUX_VERSION, OLUX_GITREV);
  int root = fdt_root();
  const char *model = fdt_getprop_str(root, "model");
  pr_info("Machine: %s\n", model ? model : "unknown");
  pr_info("Kernel loaded at %#llx, DTB at %#llx, running at EL1\n", (unsigned long long)load_pa,
          (unsigned long long)dtb_pa);
  const char *args = fdt_bootargs();
  if (args) strlcpy(cmdline, args, sizeof(cmdline));
  pr_info("Command line: %s\n", cmdline);

  setup_memory(dtb_pa);
  memblock_dump();
  mmu_init();
  fdt_relocate(phys_to_virt(dtb_pa));
  page_alloc_init();
  kmalloc_init();
  pr_info("Memory: %llu MiB total, %llu MiB free\n", (unsigned long long)(memblock_total() >> 20),
          (unsigned long long)((nr_free_pages() * PAGE_SIZE) >> 20));

  sched_init();
  dt_probe_level(DRV_IRQCHIP);
  if (!irq_get_chip()) panic("no interrupt controller found");
  dt_probe_level(DRV_TIMER);
  time_cpu_init();
  dt_probe_level(DRV_CONSOLE);

  struct thread *init = kthread_create(kernel_init, NULL, "init");
  if (!init) panic("cannot create init thread");
  sched_add_new(init);
  local_irq_enable();
  cpu_idle();
}
