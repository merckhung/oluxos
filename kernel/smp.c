/*
 * Secondary CPU bring-up and inter-processor interrupts.
 * CPUs are described by /cpus in the device tree; each is started with PSCI
 * CPU_ON or, on the Raspberry Pi, through the firmware spin table.
 */
#include <asm/pgtable.h>
#include <olux/fdt.h>
#include <olux/irq.h>
#include <olux/kernel.h>
#include <olux/mm.h>
#include <olux/psci.h>
#include <olux/sched.h>
#include <olux/smp.h>
#include <olux/time.h>

extern char secondary_entry[];
u64 secondary_boot_stack;
static volatile int booting_cpu = -1;

void secondary_start_kernel(void);
void smp_handle_sgi(int sgi);

static void (*call_fn)(void *);
static void *call_arg;

void smp_send_reschedule(int cpu) {
  const struct irq_chip *c = irq_get_chip();
  if (cpu != smp_processor_id() && cpus[cpu].online && c && c->send_sgi) c->send_sgi(IRQ_SGI_RESCHEDULE, cpu);
}

void smp_call_all(void (*fn)(void *), void *arg) {
  call_fn = fn;
  call_arg = arg;
  smp_wmb();
  const struct irq_chip *c = irq_get_chip();
  for (int i = 0; i < nr_cpus_possible; i++)
    if (i != smp_processor_id() && cpus[i].online) c->send_sgi(IRQ_SGI_CALL, i);
}

void smp_send_stop(void) {
  const struct irq_chip *c = irq_get_chip();
  if (!c || !c->send_sgi) return;
  for (int i = 0; i < nr_cpus_possible; i++)
    if (i != smp_processor_id() && cpus[i].online) c->send_sgi(IRQ_SGI_STOP, i);
  /* wait (bounded) until the other CPUs have parked themselves */
  u64 deadline = ktime_ns() + 200 * NSEC_PER_MSEC;
  while (nr_cpus_online > 1 && ktime_ns() < deadline) __asm__ volatile("yield");
}

void smp_handle_sgi(int sgi) {
  switch (sgi) {
    case IRQ_SGI_RESCHEDULE:
      this_cpu()->need_resched = true;
      break;
    case IRQ_SGI_CALL:
      if (call_fn) call_fn(call_arg);
      break;
    case IRQ_SGI_STOP:
      local_irq_disable();
      smp_wmb();
      this_cpu()->online = false;
      __atomic_sub_fetch(&nr_cpus_online, 1, __ATOMIC_SEQ_CST);
      psci_cpu_off(); /* power the core down if firmware allows */
      for (;;) wfi();
  }
}

void secondary_start_kernel(void) {
  int id = booting_cpu;
  struct cpu *c = &cpus[id];
  write_sysreg(tpidr_el1, c);
  write_sysreg(ttbr0_el1, virt_to_phys(empty_zero_pg_dir));
  isb();
  flush_tlb_all();
  c->mpidr = read_sysreg(mpidr_el1) & 0xff00ffffffUL;
  irq_get_chip()->cpu_init();
  sched_cpu_init();
  time_cpu_init();
  smp_wmb();
  c->online = true;
  __atomic_add_fetch(&nr_cpus_online, 1, __ATOMIC_SEQ_CST);
  local_irq_enable();
  cpu_idle();
}

static int start_spin_table(int node, int id) {
  int len;
  const u32 *rel = fdt_getprop(node, "cpu-release-addr", &len);
  if (!rel) return -EINVAL;
  u64 addr = fdt_read_cells(rel, len / 4);
  volatile u64 *slot = phys_to_virt(addr);
  *slot = virt_to_phys(secondary_entry);
  dcache_clean_range((void *)slot, 8);
  dsb(sy);
  sev();
  return 0;
}

void smp_init(void) {
  int cpus_node = fdt_path_offset("/cpus");
  if (cpus_node < 0) return;
  u64 boot_mpidr = read_sysreg(mpidr_el1) & 0xff00ffffffUL;
  int id = 1;
  for (int n = fdt_first_child(cpus_node); n >= 0; n = fdt_next_sibling(n)) {
    const char *type = fdt_getprop_str(n, "device_type");
    if (!type || strcmp(type, "cpu") || !fdt_is_available(n)) continue;
    int len;
    const u32 *reg = fdt_getprop(n, "reg", &len);
    if (!reg) continue;
    u64 mpidr = fdt_read_cells(reg, len / 4) & 0xff00ffffffUL;
    if (mpidr == boot_mpidr) continue;
    if (id >= NR_CPUS) break;
    cpus[id].id = id;
    cpus[id].mpidr = mpidr;
    nr_cpus_possible = id + 1;
    void *stack = vmap_stack(THREAD_STACK_SIZE);
    if (!stack) break;
    secondary_boot_stack = (u64)stack;
    booting_cpu = id;
    smp_wmb();
    const char *method = fdt_getprop_str(n, "enable-method");
    int r;
    if (method && !strcmp(method, "spin-table")) r = start_spin_table(n, id);
    else r = psci_cpu_on(mpidr, virt_to_phys(secondary_entry));
    if (r) {
      pr_warn("smp: failed to start CPU%d (mpidr %#llx): %d\n", id, (unsigned long long)mpidr, r);
      continue;
    }
    u64 deadline = ktime_ns() + NSEC_PER_SEC;
    while (!cpus[id].online && ktime_ns() < deadline) __asm__ volatile("yield");
    if (!cpus[id].online) {
      pr_warn("smp: CPU%d did not come online\n", id);
      continue;
    }
    id++;
  }
  pr_info("smp: %d CPU(s) online\n", nr_cpus_online);
}
