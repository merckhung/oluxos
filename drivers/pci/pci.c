/*
 * PCI core: bus enumeration (through bridges), BAR sizing and assignment
 * from the host bridge's memory windows, legacy INTx routing through the
 * host's "interrupt-map" (with bridge swizzling) and driver binding.
 * Also the generic ECAM host bridge (QEMU virt, "pci-host-ecam-generic").
 */
#include <olux/device.h>
#include <olux/fdt.h>
#include <olux/kernel.h>
#include <olux/mm.h>
#include <olux/pci.h>

extern const struct pci_driver __start_pci_drivers[], __stop_pci_drivers[];
static LIST_HEAD(pci_devices);

/* ---------------- configuration space ---------------- */

static volatile void *cfg(struct pci_dev *d, unsigned off) { return d->host->cfg(d->host, d->bus, d->dev, d->fn, off); }

u32 pci_read32(struct pci_dev *d, unsigned off) {
  volatile void *p = cfg(d, off & ~3u);
  return p ? readl(p) : ~0u;
}
u16 pci_read16(struct pci_dev *d, unsigned off) { return (u16)(pci_read32(d, off) >> ((off & 2) * 8)); }
u8 pci_read8(struct pci_dev *d, unsigned off) { return (u8)(pci_read32(d, off) >> ((off & 3) * 8)); }

void pci_write32(struct pci_dev *d, unsigned off, u32 v) {
  volatile void *p = cfg(d, off & ~3u);
  if (p) writel(v, p);
}
void pci_write16(struct pci_dev *d, unsigned off, u16 v) {
  u32 w = pci_read32(d, off), sh = (off & 2) * 8;
  pci_write32(d, off, (w & ~(0xffffu << sh)) | (u32)v << sh);
}
void pci_write8(struct pci_dev *d, unsigned off, u8 v) {
  u32 w = pci_read32(d, off), sh = (off & 3) * 8;
  pci_write32(d, off, (w & ~(0xffu << sh)) | (u32)v << sh);
}

void pci_set_master(struct pci_dev *d) {
  pci_write16(d, PCI_COMMAND, pci_read16(d, PCI_COMMAND) | PCI_COMMAND_MASTER | PCI_COMMAND_MEMORY);
}

void *pci_map_bar(struct pci_dev *d, int bar) {
  if (bar < 0 || bar > 5 || !d->bar[bar] || d->bar_io[bar]) return NULL;
  return ioremap(d->bar[bar], d->bar_size[bar]);
}

u64 pci_bus_addr(struct pci_dev *d, phys_addr_t pa) {
  return dt_dma_addr(fdt_first_child(d->host->node) >= 0 ? fdt_first_child(d->host->node) : d->host->node, pa);
}

/* ---------------- host windows ---------------- */

void pci_host_parse_ranges(struct pci_host *h) {
  int len;
  const u32 *r = fdt_getprop(h->node, "ranges", &len);
  int pac = fdt_address_cells(h->node), sc = 2;
  fdt_getprop_u32(h->node, "#size-cells", (u32 *)&sc);
  int stride = 3 + pac + sc;
  for (int i = 0; r && (i + stride) * 4 <= len; i += stride) {
    u32 hi = fdt32(r[i]);
    u64 pci = fdt_read_cells(&r[i + 1], 2), cpu = fdt_read_cells(&r[i + 3], pac),
        size = fdt_read_cells(&r[i + 3 + pac], sc);
    unsigned space = (hi >> 24) & 3; /* 1 io, 2 mem32, 3 mem64 */
    if (space == 2 && !h->mem_size) {
      h->mem_cpu = cpu, h->mem_pci = pci, h->mem_size = size, h->mem_next = pci;
    } else if (space == 3 && !h->mem64_size) {
      h->mem64_cpu = cpu, h->mem64_pci = pci, h->mem64_size = size, h->mem64_next = pci;
    }
  }
}

/* Allocate `size` (a power of two) bytes of PCI memory; returns the PCI address or 0. */
static u64 alloc_mem(struct pci_host *h, u64 size, bool allow64, u64 *cpu) {
  u64 a = ALIGN_UP(h->mem_next, size);
  if (h->mem_size && a + size <= h->mem_pci + h->mem_size) {
    h->mem_next = a + size;
    *cpu = h->mem_cpu + (a - h->mem_pci);
    return a;
  }
  if (allow64 && h->mem64_size) {
    a = ALIGN_UP(h->mem64_next, size);
    if (a + size <= h->mem64_pci + h->mem64_size) {
      h->mem64_next = a + size;
      *cpu = h->mem64_cpu + (a - h->mem64_pci);
      return a;
    }
  }
  return 0;
}

static void assign_bars(struct pci_dev *d, int nbars) {
  u16 cmd = pci_read16(d, PCI_COMMAND);
  pci_write16(d, PCI_COMMAND, cmd & ~(PCI_COMMAND_MEMORY | PCI_COMMAND_IO)); /* decode off while sizing */
  bool any_mem = false;
  for (int i = 0; i < nbars; i++) {
    unsigned off = PCI_BAR0 + 4 * i;
    u32 orig = pci_read32(d, off);
    pci_write32(d, off, ~0u);
    u32 sz = pci_read32(d, off);
    pci_write32(d, off, orig);
    if (!sz || sz == ~0u) continue;
    if (orig & 1) { /* I/O space: not used on ARM */
      d->bar_io[i] = true;
      continue;
    }
    bool is64 = ((orig >> 1) & 3) == 2;
    u64 mask = sz & ~0xfu;
    if (is64) {
      u32 ohi = pci_read32(d, off + 4);
      pci_write32(d, off + 4, ~0u);
      u32 shi = pci_read32(d, off + 4);
      pci_write32(d, off + 4, ohi);
      mask |= (u64)shi << 32;
    } else {
      mask |= 0xffffffff00000000ULL;
    }
    u64 size = ~mask + 1;
    u64 cpu = 0, pa = alloc_mem(d->host, size, is64, &cpu);
    if (!pa) {
      pr_warn("pci %02x:%02x.%u: no room for BAR%d (%llu bytes)\n", d->bus, d->dev, d->fn, i, (unsigned long long)size);
    } else {
      pci_write32(d, off, (u32)pa | (orig & 0xf));
      if (is64) pci_write32(d, off + 4, (u32)(pa >> 32));
      d->bar[i] = cpu;
      d->bar_size[i] = size;
      any_mem = true;
    }
    if (is64) i++;
  }
  if (any_mem) cmd |= PCI_COMMAND_MEMORY;
  pci_write16(d, PCI_COMMAND, cmd);
}

/* ---------------- interrupts ---------------- */

/* Map (bus 0 device, pin) through the host's interrupt-map. */
static int map_intx(struct pci_host *h, u8 bus, u8 dev, u8 fn, u8 pin) {
  int len, mlen;
  const u32 *map = fdt_getprop(h->node, "interrupt-map", &len);
  const u32 *mask = fdt_getprop(h->node, "interrupt-map-mask", &mlen);
  if (!map) return -1;
  u32 key[4] = {(u32)bus << 16 | (u32)dev << 11 | (u32)fn << 8, 0, 0, pin};
  u32 m[4] = {~0u, ~0u, ~0u, ~0u};
  if (mask && mlen >= 16)
    for (int i = 0; i < 4; i++) m[i] = fdt32(mask[i]);
  int i = 0, n = len / 4;
  while (i + 5 <= n) {
    bool match = true;
    for (int k = 0; k < 4; k++)
      if ((fdt32(map[i + k]) & m[k]) != (key[k] & m[k])) match = false;
    int ctrl = fdt_node_by_phandle(fdt32(map[i + 4]));
    u32 pac = 0, pic = 3;
    if (ctrl < 0) return -1;
    fdt_getprop_u32(ctrl, "#address-cells", &pac);
    fdt_getprop_u32(ctrl, "#interrupt-cells", &pic);
    const u32 *spec = &map[i + 5 + pac];
    if (match) {
      u32 irq, flags;
      return fdt_gic_spec_to_irq(spec, pic, &irq, &flags) ? -1 : (int)irq;
    }
    i += 5 + (int)pac + (int)pic;
  }
  return -1;
}

static int route_irq(struct pci_dev *d) {
  u8 pin = d->pin;
  if (!pin) return -1;
  struct pci_dev *cur = d;
  /* swizzle up to the root bus */
  while (cur->parent_bridge) {
    pin = (u8)(((pin - 1 + cur->dev) % 4) + 1);
    cur = cur->parent_bridge;
  }
  return map_intx(d->host, cur->bus, cur->dev, cur->fn, pin);
}

/* ---------------- enumeration ---------------- */

static void scan_bus(struct pci_host *h, u8 bus, struct pci_dev *bridge);

static void scan_fn(struct pci_host *h, u8 bus, u8 dev, u8 fn, struct pci_dev *bridge, u8 *hdr_out) {
  struct pci_dev tmp = {.host = h, .bus = bus, .dev = dev, .fn = fn};
  u16 vendor = pci_read16(&tmp, PCI_VENDOR_ID);
  *hdr_out = 0;
  if (vendor == 0xffff || vendor == 0) return;
  struct pci_dev *d = kzalloc(sizeof(*d), 0);
  if (!d) return;
  *d = tmp;
  d->vendor = vendor;
  d->device = pci_read16(d, PCI_DEVICE_ID);
  d->class = pci_read32(d, PCI_CLASS_REVISION) >> 8;
  d->parent_bridge = bridge;
  u8 hdr = pci_read8(d, PCI_HEADER_TYPE);
  *hdr_out = hdr;
  list_add_tail(&d->link, &pci_devices);
  if ((hdr & 0x7f) == 1) { /* PCI-to-PCI bridge */
    if (!(bridge == NULL && h->root_port_no_bars)) assign_bars(d, 2);
    u8 sec = ++h->next_bus;
    pci_write32(d, PCI_PRIMARY_BUS, (pci_read32(d, PCI_PRIMARY_BUS) & 0xff000000u) | 0xffu << 16 | (u32)sec << 8 | bus);
    h->mem_next = ALIGN_UP(h->mem_next, 1u << 20);
    u64 base = h->mem_next;
    scan_bus(h, sec, d);
    h->mem_next = ALIGN_UP(h->mem_next, 1u << 20);
    u64 limit = h->mem_next;
    pci_write8(d, PCI_PRIMARY_BUS + 2, h->next_bus); /* subordinate */
    if (limit > base)
      pci_write32(d, PCI_MEMORY_BASE, (u32)((base >> 16) & 0xfff0) | (u32)((limit - 1) & 0xfff00000));
    else
      pci_write32(d, PCI_MEMORY_BASE, 0x0000fff0);    /* closed */
    pci_write32(d, PCI_PREF_MEMORY_BASE, 0x0000fff0); /* no prefetchable window */
    pci_write32(d, PCI_PREF_BASE_UPPER32, 0);
    pci_write32(d, PCI_PREF_LIMIT_UPPER32, 0);
    pci_write16(d, PCI_IO_BASE, 0x00f0);
    pci_write16(d, PCI_COMMAND, pci_read16(d, PCI_COMMAND) | PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER);
  } else {
    assign_bars(d, 6);
  }
  d->pin = pci_read8(d, PCI_INTERRUPT_PIN);
  d->irq = route_irq(d);
  pr_info("pci %02x:%02x.%u: %04x:%04x class %06x%s\n", bus, dev, fn, d->vendor, d->device, d->class,
          (hdr & 0x7f) == 1 ? " (bridge)" : "");
}

static void scan_bus(struct pci_host *h, u8 bus, struct pci_dev *bridge) {
  for (u8 dev = 0; dev < 32; dev++) {
    u8 hdr;
    scan_fn(h, bus, dev, 0, bridge, &hdr);
    if (hdr & 0x80)
      for (u8 fn = 1; fn < 8; fn++) {
        u8 h2;
        scan_fn(h, bus, dev, fn, bridge, &h2);
      }
  }
}

int pci_host_scan(struct pci_host *h) {
  struct list_head *before = pci_devices.prev;
  scan_bus(h, h->next_bus, NULL);
  /* bind drivers to the new devices */
  for (struct list_head *e = before->next; e != &pci_devices; e = e->next) {
    struct pci_dev *d = list_entry(e, struct pci_dev, link);
    for (const struct pci_driver *drv = __start_pci_drivers; drv < __stop_pci_drivers; drv++) {
      bool idm =
          (drv->vendor == PCI_ANY || drv->vendor == d->vendor) && (drv->device == PCI_ANY || drv->device == d->device);
      bool clm = (d->class & drv->class_mask) == drv->class;
      if (idm && clm && drv->probe(d) == 0) break;
    }
  }
  return 0;
}

/* ---------------- generic ECAM host bridge ---------------- */

struct ecam {
  phys_addr_t base;
  u64 size;
  u8 bus_start;
  void *buses[256];
};

static void *ecam_cfg(struct pci_host *h, u8 bus, u8 dev, u8 fn, unsigned off) {
  struct ecam *e = h->priv;
  if (bus < e->bus_start || ((u64)(bus - e->bus_start) + 1) << 20 > e->size) return NULL;
  if (!e->buses[bus]) {
    e->buses[bus] = ioremap(e->base + ((u64)(bus - e->bus_start) << 20), 1u << 20);
    if (!e->buses[bus]) return NULL;
  }
  return (u8 *)e->buses[bus] + ((u32)dev << 15 | (u32)fn << 12 | (off & 0xffc));
}

static int ecam_probe(int node) {
  struct pci_host *h = kzalloc(sizeof(*h), 0);
  struct ecam *e = kzalloc(sizeof(*e), 0);
  if (!h || !e) return -ENOMEM;
  u64 base, size;
  if (fdt_get_reg(node, 0, &base, &size)) return -EINVAL;
  e->base = base;
  e->size = size;
  int len;
  const u32 *br = fdt_getprop(node, "bus-range", &len);
  if (br && len >= 8) e->bus_start = (u8)fdt32(br[0]);
  h->node = node;
  h->cfg = ecam_cfg;
  h->priv = e;
  h->next_bus = e->bus_start;
  pci_host_parse_ranges(h);
  pr_info("pci: ECAM host at %#llx, memory window %#llx+%#llx\n", (unsigned long long)base,
          (unsigned long long)h->mem_cpu, (unsigned long long)h->mem_size);
  return pci_host_scan(h);
}

DT_DRIVER(pci_ecam, DRV_BUS, ecam_probe, "pci-host-ecam-generic");
