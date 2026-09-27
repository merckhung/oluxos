#include <olux/kernel.h>
#include <olux/mm.h>
#include <olux/uaccess.h>

extern const struct exception_table_entry __start_ex_table[], __stop_ex_table[];

static const struct exception_table_entry *search_ex(u64 pc) {
  // cppcheck-suppress comparePointers ; linker-section bounds
  for (const struct exception_table_entry *e = __start_ex_table; e < __stop_ex_table; e++)
    if (e->insn == pc) return e;
  return NULL;
}

bool in_uaccess(u64 pc) { return search_ex(pc) != NULL; }

bool fixup_exception(u64 *pc) {
  const struct exception_table_entry *e = search_ex(*pc);
  if (!e) return false;
  *pc = e->fixup;
  return true;
}

long strncpy_from_user(char *dst, u64 src, long size) {
  if (size <= 0) return -ENAMETOOLONG;
  if (!access_ok(src, 1)) return -EFAULT;
  long max = size;
  if (src + max > USER_VA_END) max = USER_VA_END - src;
  long n = __arch_strnlen_user(src, max);
  if (n == 0) return -EFAULT;
  if (n > max) return max == size ? -ENAMETOOLONG : -EFAULT;
  if (__arch_copy_from_user(dst, src, n)) return -EFAULT;
  dst[n - 1] = '\0'; /* string may have changed under us */
  return n - 1;
}

char *strndup_user(u64 src, long max, int *err) {
  char *buf = kmalloc(max, 0);
  if (!buf) {
    *err = -ENOMEM;
    return NULL;
  }
  long n = strncpy_from_user(buf, src, max);
  if (n < 0) {
    kfree(buf);
    *err = (int)n;
    return NULL;
  }
  *err = 0;
  return buf;
}
