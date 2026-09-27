#ifndef OLUX_UACCESS_H
#define OLUX_UACCESS_H

#include <asm/memory.h>
#include <olux/errno.h>
#include <olux/types.h>

/*
 * User memory access. All accessors use unprivileged loads/stores
 * (LDTR/STTR), so the MMU checks the EL0 permissions of every access: a
 * user pointer into kernel memory or a read-only page faults and the
 * access fails with -EFAULT via the exception fixup table.
 */
static inline bool access_ok(u64 addr, u64 size) {
  return addr <= USER_VA_END && size <= USER_VA_END - addr;
}

/* Return the number of bytes NOT copied. */
unsigned long __arch_copy_from_user(void *to, u64 from, unsigned long n);
unsigned long __arch_copy_to_user(u64 to, const void *from, unsigned long n);
unsigned long __arch_clear_user(u64 to, unsigned long n);
long __arch_strnlen_user(u64 s, long max); /* length incl. NUL, 0 on fault */

static inline int copy_from_user(void *to, u64 from, unsigned long n) {
  if (!access_ok(from, n)) return -EFAULT;
  return __arch_copy_from_user(to, from, n) ? -EFAULT : 0;
}
static inline int copy_to_user(u64 to, const void *from, unsigned long n) {
  if (!access_ok(to, n)) return -EFAULT;
  return __arch_copy_to_user(to, from, n) ? -EFAULT : 0;
}
static inline int clear_user(u64 to, unsigned long n) {
  if (!access_ok(to, n)) return -EFAULT;
  return __arch_clear_user(to, n) ? -EFAULT : 0;
}
#define get_user(x, uptr) copy_from_user(&(x), (u64)(uptr), sizeof(x))
#define put_user(x, uptr)                          \
  ({                                               \
    __typeof__(x) __pv = (x);                      \
    copy_to_user((u64)(uptr), &__pv, sizeof(__pv)); \
  })

/* Copy a NUL-terminated string. Returns length (excluding NUL),
 * -EFAULT, or -ENAMETOOLONG if it does not fit in `size`. */
long strncpy_from_user(char *dst, u64 src, long size);
/* Duplicate a user string into kmalloc'ed memory (max PATH_MAX). */
char *strndup_user(u64 src, long max, int *err);

struct exception_table_entry {
  u64 insn, fixup;
};
bool fixup_exception(u64 *pc);
bool in_uaccess(u64 pc);

#endif
