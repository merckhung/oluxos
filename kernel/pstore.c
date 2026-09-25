/*
 * pstore: the kernel log is mirrored into a RAM region that survives a
 * warm reset (reboot, panic reboot, watchdog reset), so after the restart
 * the previous boot's last messages, including a panic backtrace, can be
 * read from /proc/last_kmsg, and the boot log says how that boot ended.
 *
 * The region is the device tree's reserved-memory node compatible with
 * "ramoops" (as in Linux) if there is one, else 64 KiB at the top of the
 * first memory bank. It is mapped non-cacheable, so a reset never loses
 * data still sitting in the caches. A cold power-on leaves garbage, which
 * the header's magic and checksum reject.
 */
#include <asm/pgtable.h>
#include <olux/fdt.h>
#include <olux/kernel.h>
#include <olux/mm.h>
#include <olux/pstore.h>

#define PSTORE_MAGIC 0x504c584fu /* "OXLP" */
#define DEFAULT_SIZE (64 * 1024)

struct pstore_hdr {
  u32 magic, size; /* size of the log area after the header */
  u64 head;        /* total bytes ever written */
  u32 state, boot; /* enum pstore_state, boot counter */
  u32 csum, pad;
};

static phys_addr_t region_pa;
static u64 region_size;
static volatile struct pstore_hdr *hdr;
static volatile char *data;
static char *last_log;
static size_t last_len;
static u32 last_state, last_boot;
static bool have_last;

static u32 csum(const volatile struct pstore_hdr *h) {
  return h->magic ^ h->size ^ (u32)h->head ^ (u32)(h->head >> 32) ^ h->state ^ h->boot ^ 0x5a5a5a5au;
}

static void first_bank(u64 base, u64 size, void *arg) {
  u64 *top = arg;
  if (!*top) top[0] = base + size, top[1] = base;
}

void pstore_reserve(void) {
  int node = fdt_find_compatible(-1, "ramoops");
  u64 a, s;
  if (node >= 0 && fdt_get_reg(node, 0, &a, &s) == 0 && s >= 4096) {
    region_pa = a;
    region_size = s;
  } else {
    u64 bank[2] = {0, 0};
    fdt_for_each_memory(first_bank, bank);
    if (bank[0] - bank[1] < 64ULL << 20) return; /* tiny machine: not worth it */
    region_pa = (bank[0] - DEFAULT_SIZE) & ~(u64)(PAGE_SIZE - 1);
    region_size = DEFAULT_SIZE;
  }
  memblock_reserve(region_pa, region_size);
}

void pstore_init(void) {
  if (!region_size) return;
  /* drop any stale lines of the cacheable linear-map alias */
  for (u64 o = 0; o < region_size; o += 64) __asm__ volatile("dc ivac, %0" ::"r"(phys_to_virt(region_pa + o)) : "memory");
  __asm__ volatile("dsb sy" ::: "memory");
  void *m = ioremap_prot(region_pa, region_size, PROT_NORMAL_NC);
  if (!m) return;
  volatile struct pstore_hdr *h = m;
  volatile char *d = (volatile char *)m + sizeof(*h);
  u32 dsize = (u32)(region_size - sizeof(*h));
  if (h->magic == PSTORE_MAGIC && h->size == dsize && h->csum == csum(h)) {
    have_last = true;
    last_state = h->state;
    last_boot = h->boot;
    u64 start = h->head > dsize ? h->head - dsize : 0;
    last_len = (size_t)(h->head - start);
    last_log = kmalloc(last_len + 1, 0);
    if (last_log) {
      for (size_t i = 0; i < last_len; i++) last_log[i] = d[(start + i) % dsize];
      last_log[last_len] = 0;
    }
  }
  h->magic = PSTORE_MAGIC;
  h->size = dsize;
  h->head = 0;
  h->state = PSTORE_RUNNING;
  h->boot = have_last ? last_boot + 1 : 1;
  h->csum = csum(h);
  data = d;
  /* copy this boot's log so far, then printk keeps the mirror current */
  char buf[256];
  size_t pos = 0, n;
  hdr = h;
  while ((n = klog_read(buf, sizeof(buf), &pos))) pstore_write(buf, n);
  pr_info("pstore: %llu KiB at %#llx, boot #%u\n", (unsigned long long)(region_size >> 10),
          (unsigned long long)region_pa, hdr->boot);
  if (have_last)
    pr_notice("pstore: previous boot #%u ended with %s; its log is in /proc/last_kmsg\n", last_boot,
              pstore_last_reason());
}

/* Called with the printk lock held (or in panic). */
void pstore_write(const char *s, size_t n) {
  volatile struct pstore_hdr *h = hdr;
  if (!h) return;
  u32 dsize = h->size;
  u64 head = h->head;
  for (size_t i = 0; i < n; i++) data[(head + i) % dsize] = s[i];
  h->head = head + n;
  h->csum = csum(h);
}

void pstore_set_state(enum pstore_state st) {
  volatile struct pstore_hdr *h = hdr;
  if (!h) return;
  if (st == PSTORE_RESTART || st == PSTORE_POWEROFF) /* keep the more specific cause */
    if (h->state != PSTORE_RUNNING) return;
  h->state = st;
  h->csum = csum(h);
  __asm__ volatile("dsb sy" ::: "memory");
}

const char *pstore_last_log(size_t *len) {
  *len = last_len;
  return last_log;
}

const char *pstore_last_reason(void) {
  if (!have_last) return "none";
  switch (last_state) {
    case PSTORE_RESTART:
      return "a reboot";
    case PSTORE_POWEROFF:
      return "a halt or power-off";
    case PSTORE_PANIC:
      return "a kernel panic";
    case PSTORE_WATCHDOG:
      return "a watchdog reset";
    default:
      return "an unexpected reset (hang, hardware watchdog or reset button)";
  }
}
