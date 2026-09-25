#include <olux/fdt.h>
#include <olux/kernel.h>

#define FDT_BEGIN_NODE 1
#define FDT_END_NODE 2
#define FDT_PROP 3
#define FDT_NOP 4
#define FDT_END 9

struct fdt_header {
  u32 magic, totalsize, off_dt_struct, off_dt_strings, off_mem_rsvmap, version, last_comp_version, boot_cpuid_phys,
      size_dt_strings, size_dt_struct;
};

static const u8 *blob;
static const u8 *dt_struct;
static const char *dt_strings;
static u32 struct_size, strings_size;

static inline u32 rd32(const void *p) {
  const u8 *b = p;
  return ((u32)b[0] << 24) | ((u32)b[1] << 16) | ((u32)b[2] << 8) | b[3];
}

u32 fdt_totalsize(const void *b) { return rd32((const u8 *)b + 4); }

int fdt_init(const void *b) {
  const struct fdt_header *h = b;
  if (!b || rd32(&h->magic) != FDT_MAGIC) return FDT_ERR_BADBLOB;
  if (rd32(&h->version) < 16 || rd32(&h->last_comp_version) > 17) return FDT_ERR_BADBLOB;
  u32 total = rd32(&h->totalsize);
  u32 so = rd32(&h->off_dt_struct), ss = rd32(&h->size_dt_struct);
  u32 to = rd32(&h->off_dt_strings), ts = rd32(&h->size_dt_strings);
  if (so + ss > total || to + ts > total || so + ss < so || to + ts < to) return FDT_ERR_BADBLOB;
  blob = b;
  dt_struct = blob + so;
  dt_strings = (const char *)blob + to;
  struct_size = ss;
  strings_size = ts;
  return 0;
}

const void *fdt_blob(void) { return blob; }

void fdt_relocate(const void *nb) { fdt_init(nb); }

/* Token stream walking. `off` points at a token; returns next token offset. */
static int next_token(int off, u32 *tag) {
  if (off < 0 || (u32)off + 4 > struct_size) return -1;
  u32 t = rd32(dt_struct + off);
  *tag = t;
  off += 4;
  switch (t) {
    case FDT_BEGIN_NODE: {
      const char *name = (const char *)dt_struct + off;
      size_t max = struct_size - off;
      size_t len = strnlen(name, max);
      if (len == max) return -1;
      off += ALIGN_UP(len + 1, 4);
      break;
    }
    case FDT_PROP: {
      if ((u32)off + 8 > struct_size) return -1;
      u32 len = rd32(dt_struct + off);
      if (len > struct_size - off - 8) return -1;
      off += 8 + ALIGN_UP(len, 4);
      break;
    }
    case FDT_END_NODE:
    case FDT_NOP:
    case FDT_END:
      break;
    default:
      return -1;
  }
  return off;
}

int fdt_root(void) {
  int off = 0;
  u32 tag;
  while (off >= 0) {
    int n = next_token(off, &tag);
    if (tag == FDT_BEGIN_NODE) return off;
    if (tag != FDT_NOP) return FDT_ERR_BADBLOB;
    off = n;
  }
  return FDT_ERR_BADBLOB;
}

const char *fdt_node_name(int node) {
  if (node < 0) return "";
  return (const char *)dt_struct + node + 4;
}

/* Offset just past node's BEGIN_NODE and properties (first child or END). */
static int skip_props(int node, u32 *tag) {
  int off = next_token(node, tag);
  while (off >= 0) {
    int n = next_token(off, tag);
    if (*tag != FDT_PROP && *tag != FDT_NOP) return off;
    off = n;
  }
  return -1;
}

/* Offset just past the END_NODE of node. */
static int skip_node(int node) {
  u32 tag;
  int depth = 0, off = node;
  while (off >= 0) {
    int n = next_token(off, &tag);
    if (tag == FDT_BEGIN_NODE)
      depth++;
    else if (tag == FDT_END_NODE && --depth == 0)
      return n;
    else if (tag == FDT_END)
      return -1;
    off = n;
  }
  return -1;
}

int fdt_first_child(int node) {
  u32 tag;
  int off = skip_props(node, &tag);
  if (off < 0 || tag != FDT_BEGIN_NODE) return FDT_ERR_NOTFOUND;
  return off;
}

int fdt_next_sibling(int node) {
  int off = skip_node(node);
  u32 tag;
  while (off >= 0) {
    int n = next_token(off, &tag);
    if (tag == FDT_BEGIN_NODE) return off;
    if (tag != FDT_NOP) return FDT_ERR_NOTFOUND;
    off = n;
  }
  return FDT_ERR_NOTFOUND;
}

int fdt_parent(int node) {
  /* Walk from the root tracking the path stack. */
  int stack[32];
  int depth = -1, off = 0;
  u32 tag;
  while (off >= 0) {
    int n = next_token(off, &tag);
    if (tag == FDT_BEGIN_NODE) {
      if (off == node) return depth >= 0 ? stack[depth] : FDT_ERR_NOTFOUND;
      if (++depth >= (int)ARRAY_SIZE(stack)) return FDT_ERR_BADBLOB;
      stack[depth] = off;
    } else if (tag == FDT_END_NODE) {
      depth--;
    } else if (tag == FDT_END) {
      break;
    }
    off = n;
  }
  return FDT_ERR_NOTFOUND;
}

const void *fdt_getprop(int node, const char *name, int *lenp) {
  if (node < 0) return NULL;
  u32 tag;
  int off = next_token(node, &tag);
  while (off >= 0) {
    int n = next_token(off, &tag);
    if (n < 0) return NULL;
    if (tag == FDT_PROP) {
      u32 len = rd32(dt_struct + off + 4);
      u32 nameoff = rd32(dt_struct + off + 8);
      if (nameoff < strings_size && !strncmp(dt_strings + nameoff, name, strings_size - nameoff) &&
          strlen(name) < strings_size - nameoff) {
        if (lenp) *lenp = len;
        return dt_struct + off + 12;
      }
    } else if (tag != FDT_NOP) {
      return NULL;
    }
    off = n;
  }
  return NULL;
}

bool fdt_getprop_u32(int node, const char *name, u32 *out) {
  int len;
  const u32 *p = fdt_getprop(node, name, &len);
  if (!p || len < 4) return false;
  *out = rd32(p);
  return true;
}

const char *fdt_getprop_str(int node, const char *name) {
  int len;
  const char *p = fdt_getprop(node, name, &len);
  if (!p || len <= 0 || p[len - 1] != '\0') return NULL;
  return p;
}

bool fdt_is_compatible(int node, const char *compat) {
  int len;
  const char *p = fdt_getprop(node, "compatible", &len);
  if (!p) return false;
  while (len > 0) {
    size_t l = strnlen(p, len);
    if (!strcmp(p, compat)) return true;
    p += l + 1;
    len -= l + 1;
  }
  return false;
}

bool fdt_is_available(int node) {
  const char *s = fdt_getprop_str(node, "status");
  return !s || !strcmp(s, "okay") || !strcmp(s, "ok");
}

int fdt_find_compatible(int from, const char *compat) {
  u32 tag;
  int off = from < 0 ? 0 : next_token(from, &tag);
  while (off >= 0) {
    int n = next_token(off, &tag);
    if (tag == FDT_BEGIN_NODE && fdt_is_compatible(off, compat)) return off;
    if (tag == FDT_END) break;
    off = n;
  }
  return FDT_ERR_NOTFOUND;
}

int fdt_node_by_phandle(u32 ph) {
  u32 tag;
  int off = 0;
  while (off >= 0) {
    int n = next_token(off, &tag);
    if (tag == FDT_BEGIN_NODE) {
      u32 v;
      if ((fdt_getprop_u32(off, "phandle", &v) || fdt_getprop_u32(off, "linux,phandle", &v)) && v == ph) return off;
    }
    if (tag == FDT_END) break;
    off = n;
  }
  return FDT_ERR_NOTFOUND;
}

int fdt_path_offset(const char *path) {
  int node = fdt_root();
  if (*path != '/') {
    /* alias */
    int aliases = fdt_path_offset("/aliases");
    const char *p = aliases >= 0 ? fdt_getprop_str(aliases, path) : NULL;
    return p ? fdt_path_offset(p) : FDT_ERR_NOTFOUND;
  }
  while (*path == '/') path++;
  while (*path && node >= 0) {
    const char *end = strchr(path, '/');
    size_t len = end ? (size_t)(end - path) : strlen(path);
    int child;
    for (child = fdt_first_child(node); child >= 0; child = fdt_next_sibling(child)) {
      const char *nm = fdt_node_name(child);
      if (!strncmp(nm, path, len) && (nm[len] == '\0' || (nm[len] == '@' && !memchr(path, '@', len)))) break;
    }
    node = child;
    path += len;
    while (*path == '/') path++;
  }
  return node;
}

static int cells_of(int parent, const char *prop, int dflt) {
  u32 v;
  if (parent >= 0 && fdt_getprop_u32(parent, prop, &v) && v <= 4) return v;
  return dflt;
}

int fdt_address_cells(int node) { return cells_of(fdt_parent(node), "#address-cells", 2); }
int fdt_size_cells(int node) { return cells_of(fdt_parent(node), "#size-cells", 1); }

u64 fdt_read_cells(const u32 *c, int n) {
  u64 v = 0;
  for (int i = 0; i < n; i++) v = (v << 32) | rd32(&c[i]);
  return v;
}

/* Translate a bus address through the "ranges" of every ancestor bus. */
static bool translate(int bus, u64 *addr) {
  while (bus >= 0) {
    int parent = fdt_parent(bus);
    if (parent < 0) return true; /* reached the root */
    int len;
    const u32 *r = fdt_getprop(bus, "ranges", &len);
    if (!r) return false; /* not translatable */
    if (len == 0) {       /* identity */
      bus = parent;
      continue;
    }
    int ca = cells_of(bus, "#address-cells", 2);
    int cs = cells_of(bus, "#size-cells", 1);
    int pa = cells_of(parent, "#address-cells", 2);
    int entry = ca + pa + cs;
    bool found = false;
    for (int i = 0; (i + 1) * entry * 4 <= len; i++) {
      const u32 *e = r + i * entry;
      u64 child = fdt_read_cells(e, ca);
      u64 paddr = fdt_read_cells(e + ca, pa);
      u64 size = fdt_read_cells(e + ca + pa, cs);
      if (*addr >= child && *addr - child < size) {
        *addr = *addr - child + paddr;
        found = true;
        break;
      }
    }
    if (!found) return false;
    bus = parent;
  }
  return true;
}

int fdt_get_reg(int node, int idx, u64 *addr, u64 *size) {
  int parent = fdt_parent(node);
  int ac = cells_of(parent, "#address-cells", 2);
  int sc = cells_of(parent, "#size-cells", 1);
  int len;
  const u32 *reg = fdt_getprop(node, "reg", &len);
  int entry = (ac + sc) * 4;
  if (!reg || entry == 0 || (idx + 1) * entry > len) return -ENOENT;
  const u32 *e = reg + idx * (ac + sc);
  u64 a = fdt_read_cells(e, ac);
  u64 s = fdt_read_cells(e + ac, sc);
  if (!translate(parent, &a)) return -EINVAL;
  if (addr) *addr = a;
  if (size) *size = s;
  return 0;
}

static int irq_parent(int node) {
  for (int n = node; n >= 0; n = fdt_parent(n)) {
    u32 ph;
    if (fdt_getprop_u32(n, "interrupt-parent", &ph)) return fdt_node_by_phandle(ph);
  }
  return FDT_ERR_NOTFOUND;
}

/* Converts a GIC 3-cell specifier (type, number, flags) into an INTID. */
int fdt_gic_spec_to_irq(const u32 *spec, u32 cells, u32 *irq, u32 *flags) {
  if (cells < 3) return -EINVAL;
  u32 type = rd32(&spec[0]), num = rd32(&spec[1]);
  *irq = type == 1 ? num + 16 : num + 32;
  if (flags) *flags = rd32(&spec[2]);
  return 0;
}

int fdt_get_irq(int node, int idx, u32 *irq, u32 *flags) {
  int len;
  const u32 *ext = fdt_getprop(node, "interrupts-extended", &len);
  if (ext) {
    int pos = 0, i = 0;
    while (pos * 4 < len) {
      int ctrl = fdt_node_by_phandle(rd32(&ext[pos]));
      u32 cells = 3;
      if (ctrl < 0 || !fdt_getprop_u32(ctrl, "#interrupt-cells", &cells)) return -EINVAL;
      if (i == idx) return fdt_gic_spec_to_irq(&ext[pos + 1], cells, irq, flags);
      pos += 1 + cells;
      i++;
    }
    return -ENOENT;
  }
  const u32 *ints = fdt_getprop(node, "interrupts", &len);
  if (!ints) return -ENOENT;
  int ctrl = irq_parent(node);
  u32 cells = 3;
  if (ctrl < 0 || !fdt_getprop_u32(ctrl, "#interrupt-cells", &cells) || cells == 0) return -EINVAL;
  if ((idx + 1) * (int)cells * 4 > len) return -ENOENT;
  return fdt_gic_spec_to_irq(ints + idx * cells, cells, irq, flags);
}

bool fdt_mem_rsv(int idx, u64 *addr, u64 *size) {
  const struct fdt_header *h = (const void *)blob;
  const u8 *p = blob + rd32(&h->off_mem_rsvmap) + idx * 16;
  if (p + 16 > blob + rd32(&h->totalsize)) return false;
  *addr = ((u64)rd32(p) << 32) | rd32(p + 4);
  *size = ((u64)rd32(p + 8) << 32) | rd32(p + 12);
  return *addr || *size;
}

static void for_each_reg(int node, fdt_region_cb cb, void *arg) {
  u64 a, s;
  for (int i = 0; fdt_get_reg(node, i, &a, &s) == 0; i++)
    if (s) cb(a, s, arg);
}

void fdt_for_each_memory(fdt_region_cb cb, void *arg) {
  int root = fdt_root();
  for (int n = fdt_first_child(root); n >= 0; n = fdt_next_sibling(n)) {
    const char *t = fdt_getprop_str(n, "device_type");
    if (t && !strcmp(t, "memory") && fdt_is_available(n)) for_each_reg(n, cb, arg);
  }
}

void fdt_for_each_reserved(fdt_region_cb cb, void *arg) {
  int rm = fdt_path_offset("/reserved-memory");
  if (rm < 0) return;
  for (int n = fdt_first_child(rm); n >= 0; n = fdt_next_sibling(n))
    if (fdt_is_available(n)) for_each_reg(n, cb, arg);
}

const char *fdt_bootargs(void) {
  int c = fdt_path_offset("/chosen");
  return c >= 0 ? fdt_getprop_str(c, "bootargs") : NULL;
}

bool fdt_initrd(u64 *start, u64 *end) {
  int c = fdt_path_offset("/chosen");
  int l1, l2;
  const u32 *s = fdt_getprop(c, "linux,initrd-start", &l1);
  const u32 *e = fdt_getprop(c, "linux,initrd-end", &l2);
  if (c < 0 || !s || !e) return false;
  *start = fdt_read_cells(s, l1 / 4);
  *end = fdt_read_cells(e, l2 / 4);
  return *end > *start;
}
