#include <olux/device.h>
#include <olux/fdt.h>
#include <olux/kernel.h>
#include <olux/mm.h>

extern const struct dt_driver __start_drivers[], __stop_drivers[];
extern const initcall_t __start_initcalls[], __stop_initcalls[];

#define MAX_CLAIMED 128
static int claimed[MAX_CLAIMED];
static int nclaimed;

bool dt_node_claimed(int node) {
  for (int i = 0; i < nclaimed; i++)
    if (claimed[i] == node) return true;
  return false;
}

static const struct dt_driver *match(int node, enum driver_level level) {
  for (const struct dt_driver *d = __start_drivers; d < __stop_drivers; d++) {
    if (d->level != level) continue;
    for (const char *const *c = d->compatible; *c; c++)
      if (fdt_is_compatible(node, *c)) return d;
  }
  return NULL;
}

static void probe_subtree(int node, enum driver_level level) {
  for (int n = fdt_first_child(node); n >= 0; n = fdt_next_sibling(n)) {
    if (!fdt_is_available(n)) continue;
    if (!dt_node_claimed(n)) {
      const struct dt_driver *d = match(n, level);
      if (d) {
        int r = d->probe(n);
        if (r == 0) {
          if (nclaimed < MAX_CLAIMED) claimed[nclaimed++] = n;
        } else if (r != -ENODEV) {
          pr_warn("%s: probe of %s failed: %d\n", d->name, fdt_node_name(n), r);
        }
      }
    }
    probe_subtree(n, level);
  }
}

void dt_probe_level(enum driver_level level) { probe_subtree(fdt_root(), level); }

void do_initcalls(void) {
  for (const initcall_t *fn = __start_initcalls; fn < __stop_initcalls; fn++) {
    int r = (*fn)();
    if (r && r != -ENODEV) pr_warn("initcall %p failed: %d\n", (void *)*fn, r);
  }
}

void *dt_ioremap(int node, int idx, u64 *size_out) {
  u64 addr, size;
  if (fdt_get_reg(node, idx, &addr, &size)) return NULL;
  if (size_out) *size_out = size;
  return ioremap(addr, size);
}
