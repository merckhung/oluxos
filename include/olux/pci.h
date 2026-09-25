/* PCI / PCIe: configuration access, enumeration, BARs and interrupts. */
#ifndef OLUX_PCI_H
#define OLUX_PCI_H

#include <olux/compiler.h>
#include <olux/list.h>
#include <olux/types.h>

#define PCI_VENDOR_ID 0x00
#define PCI_DEVICE_ID 0x02
#define PCI_COMMAND 0x04
#define PCI_COMMAND_IO 0x1
#define PCI_COMMAND_MEMORY 0x2
#define PCI_COMMAND_MASTER 0x4
#define PCI_COMMAND_INTX_DISABLE 0x400
#define PCI_STATUS 0x06
#define PCI_CLASS_REVISION 0x08
#define PCI_HEADER_TYPE 0x0e
#define PCI_BAR0 0x10
#define PCI_PRIMARY_BUS 0x18
#define PCI_MEMORY_BASE 0x20
#define PCI_PREF_MEMORY_BASE 0x24
#define PCI_PREF_BASE_UPPER32 0x28
#define PCI_PREF_LIMIT_UPPER32 0x2c
#define PCI_IO_BASE 0x1c
#define PCI_INTERRUPT_PIN 0x3d
#define PCI_BRIDGE_CONTROL 0x3e

struct pci_host;

struct pci_dev {
  struct pci_host *host;
  u8 bus, dev, fn;
  u16 vendor, device;
  u32 class; /* class << 16 | subclass << 8 | prog-if */
  u8 pin;    /* INTA..INTD = 1..4, 0 = none */
  int irq;   /* -1 if none */
  u64 bar[6], bar_size[6];
  bool bar_io[6];
  struct pci_dev *parent_bridge;
  void *driver_data;
  struct list_head link;
};

struct pci_driver {
  const char *name;
  u16 vendor, device;    /* 0xffff = any */
  u32 class, class_mask; /* match (class & mask) == class */
  int (*probe)(struct pci_dev *d);
};

#define PCI_ANY 0xffff
#define PCI_DRIVER(ident, ...) \
  static const struct pci_driver ident##_pcidrv __used __section(".pci_drivers") = {__VA_ARGS__}

u32 pci_read32(struct pci_dev *d, unsigned off);
u16 pci_read16(struct pci_dev *d, unsigned off);
u8 pci_read8(struct pci_dev *d, unsigned off);
void pci_write32(struct pci_dev *d, unsigned off, u32 v);
void pci_write16(struct pci_dev *d, unsigned off, u16 v);
void pci_write8(struct pci_dev *d, unsigned off, u8 v);

void *pci_map_bar(struct pci_dev *d, int bar);
void pci_set_master(struct pci_dev *d);
/* Bus address the device uses to reach CPU physical address `pa`. */
u64 pci_bus_addr(struct pci_dev *d, phys_addr_t pa);

/* Host bridge drivers: register a root bus with its config accessor. */
struct pci_host {
  int node;
  void *(*cfg)(struct pci_host *h, u8 bus, u8 dev, u8 fn, unsigned off); /* NULL: absent */
  u64 mem_cpu, mem_pci, mem_size, mem_next;                              /* 32-bit memory window */
  u64 mem64_cpu, mem64_pci, mem64_size, mem64_next;
  u8 next_bus;
  bool root_port_no_bars; /* the root port's BARs are host-specific (e.g. brcmstb inbound window) */
  void *priv;
};
int pci_host_scan(struct pci_host *h);
/* Parse "ranges" of a PCI host node into the windows. */
void pci_host_parse_ranges(struct pci_host *h);

#endif
