#ifndef OLUX_FDT_H
#define OLUX_FDT_H

#include <olux/types.h>

/*
 * Read-only flattened device tree access. Nodes are identified by their
 * structure-block offset (>= 0); negative values are errors / "not found".
 */
#define FDT_MAGIC 0xd00dfeed
#define FDT_ERR_NOTFOUND (-1)
#define FDT_ERR_BADBLOB (-2)

int fdt_init(const void *blob);
const void *fdt_blob(void);
void fdt_relocate(const void *new_blob);
u32 fdt_totalsize(const void *blob);

int fdt_root(void);
int fdt_path_offset(const char *path);
int fdt_first_child(int node);
int fdt_next_sibling(int node);
int fdt_parent(int node);
const char *fdt_node_name(int node);
const void *fdt_getprop(int node, const char *name, int *len);
bool fdt_getprop_u32(int node, const char *name, u32 *out);
const char *fdt_getprop_str(int node, const char *name);
int fdt_node_by_phandle(u32 phandle);
bool fdt_is_compatible(int node, const char *compat);
bool fdt_is_available(int node);
/* Next node after `from` (or from the start if from < 0) matching compat. */
int fdt_find_compatible(int from, const char *compat);
int fdt_address_cells(int node); /* #address-cells applying to node's reg */
int fdt_size_cells(int node);

/* Translate the idx'th "reg" entry to a CPU physical address. */
int fdt_get_reg(int node, int idx, u64 *addr, u64 *size);
u64 fdt_read_cells(const u32 *cells, int n);
static inline u32 fdt32(u32 v) { return __builtin_bswap32(v); }

/* Interrupt specifier for the idx'th interrupt of node, as a GIC INTID. */
int fdt_get_irq(int node, int idx, u32 *irq, u32 *flags);

/* Iterate /memreserve/ entries. Returns false at the end. */
bool fdt_mem_rsv(int idx, u64 *addr, u64 *size);

/* For each "memory" node reg entry. */
typedef void (*fdt_region_cb)(u64 base, u64 size, void *arg);
void fdt_for_each_memory(fdt_region_cb cb, void *arg);
/* For each /reserved-memory child reg entry. */
void fdt_for_each_reserved(fdt_region_cb cb, void *arg);

/* /chosen helpers */
const char *fdt_bootargs(void);
bool fdt_initrd(u64 *start, u64 *end);

#endif
