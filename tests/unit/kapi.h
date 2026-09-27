/* Kernel functions under test, as seen from host test code. */
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
void *k_memcpy(void *, const void *, size_t);
void *k_memmove(void *, const void *, size_t);
void *k_memset(void *, int, size_t);
int k_memcmp(const void *, const void *, size_t);
size_t k_strlen(const char *);
int k_strcmp(const char *, const char *);
int k_strncmp(const char *, const char *, size_t);
size_t k_strlcpy(char *, const char *, size_t);
size_t k_strlcat(char *, const char *, size_t);
char *k_strstr(const char *, const char *);
unsigned long k_strtoul(const char *, char **, int);
long k_strtol(const char *, char **, int);
int k_vsnprintf(char *, size_t, const char *, va_list);
int k_snprintf(char *, size_t, const char *, ...);

int fdt_init(const void *blob);
int fdt_root(void);
int fdt_path_offset(const char *path);
int fdt_first_child(int node);
int fdt_next_sibling(int node);
int fdt_parent(int node);
const char *fdt_node_name(int node);
const void *fdt_getprop(int node, const char *name, int *len);
int fdt_find_compatible(int from, const char *compat);
int fdt_get_reg(int node, int idx, uint64_t *addr, uint64_t *size);
int fdt_get_irq(int node, int idx, uint32_t *irq, uint32_t *flags);
typedef void (*fdt_region_cb)(uint64_t base, uint64_t size, void *arg);
void fdt_for_each_memory(fdt_region_cb cb, void *arg);
const char *fdt_bootargs(void);
