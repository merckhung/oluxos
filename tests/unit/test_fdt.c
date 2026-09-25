/* FDT parser: functional checks against QEMU's virt DTB plus a mutation
 * fuzzer that feeds corrupted blobs (must never crash; run under ASan). */
#include <string.h>
#include "check.h"
#include "kapi.h"

static unsigned char *load(const char *path, size_t *len) {
  FILE *f = fopen(path, "rb");
  if (!f) return NULL;
  unsigned char *b = malloc(1 << 20);
  *len = fread(b, 1, 1 << 20, f);
  fclose(f);
  return b;
}

static uint64_t mem_total;
static void on_mem(uint64_t base, uint64_t size, void *arg) { mem_total += size; }

static void walk_all(void) {
  int root = fdt_root();
  if (root < 0) return;
  int stack[64], sp = 0, n = fdt_first_child(root), steps = 0;
  while (n >= 0 && steps++ < 100000) {
    int len;
    fdt_getprop(n, "compatible", &len);
    uint64_t a, s;
    fdt_get_reg(n, 0, &a, &s);
    uint32_t irq, fl;
    fdt_get_irq(n, 0, &irq, &fl);
    fdt_node_name(n);
    int c = fdt_first_child(n);
    if (c >= 0 && sp < 64) {
      stack[sp++] = n;
      n = c;
      continue;
    }
    int next = fdt_next_sibling(n);
    while (next < 0 && sp > 0) next = fdt_next_sibling(stack[--sp]);
    n = next;
  }
  fdt_for_each_memory(on_mem, NULL);
  fdt_bootargs();
}

int main(int argc, char **argv) {
  size_t len;
  unsigned char *dtb = load(argc > 1 ? argv[1] : "virt.dtb", &len);
  if (!dtb) {
    printf("test_fdt: no DTB available, skipping\n");
    return 0;
  }
  CHECK(fdt_init(dtb) == 0);
  int uart = fdt_find_compatible(-1, "arm,pl011");
  CHECK(uart >= 0);
  uint64_t a, s;
  CHECK(fdt_get_reg(uart, 0, &a, &s) == 0 && a == 0x9000000 && s == 0x1000);
  uint32_t irq, fl;
  CHECK(fdt_get_irq(uart, 0, &irq, &fl) == 0 && irq == 33);
  mem_total = 0;
  fdt_for_each_memory(on_mem, NULL);
  CHECK(mem_total == 256ULL << 20);
  CHECK(fdt_path_offset("/chosen") >= 0);
  CHECK(fdt_parent(uart) == fdt_root());
  int gic = fdt_find_compatible(-1, "arm,cortex-a15-gic");
  CHECK(gic >= 0);

  /* mutation fuzzing */
  unsigned char *m = malloc(len);
  srand(12345);
  int iters = argc > 2 ? atoi(argv[2]) : 20000;
  for (int i = 0; i < iters; i++) {
    memcpy(m, dtb, len);
    int flips = 1 + rand() % 8;
    for (int k = 0; k < flips; k++) {
      size_t pos = rand() % len;
      switch (rand() % 3) {
        case 0: m[pos] ^= 1 << (rand() % 8); break;
        case 1: m[pos] = rand(); break;
        case 2: m[pos] = 0xff; break;
      }
    }
    if (fdt_init(m) == 0) walk_all();
  }
  free(m);
  free(dtb);
  return DONE();
}
