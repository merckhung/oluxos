#ifndef OLUX_KERNEL_H
#define OLUX_KERNEL_H

#include <asm/sysreg.h>
#include <olux/compiler.h>
#include <olux/errno.h>
#include <olux/printf.h>
#include <olux/string.h>
#include <olux/types.h>

#define LOGLEVEL_EMERG 0
#define LOGLEVEL_ERR 3
#define LOGLEVEL_WARN 4
#define LOGLEVEL_NOTICE 5
#define LOGLEVEL_INFO 6
#define LOGLEVEL_DEBUG 7

int printk(int level, const char *fmt, ...) __printf(2, 3);
int vprintk(int level, const char *fmt, va_list ap);
extern int console_loglevel;

#ifndef pr_fmt
#define pr_fmt(fmt) fmt
#endif
#define pr_emerg(fmt, ...) printk(LOGLEVEL_EMERG, pr_fmt(fmt), ##__VA_ARGS__)
#define pr_err(fmt, ...) printk(LOGLEVEL_ERR, pr_fmt(fmt), ##__VA_ARGS__)
#define pr_warn(fmt, ...) printk(LOGLEVEL_WARN, pr_fmt(fmt), ##__VA_ARGS__)
#define pr_notice(fmt, ...) printk(LOGLEVEL_NOTICE, pr_fmt(fmt), ##__VA_ARGS__)
#define pr_info(fmt, ...) printk(LOGLEVEL_INFO, pr_fmt(fmt), ##__VA_ARGS__)
#define pr_debug(fmt, ...) printk(LOGLEVEL_DEBUG, pr_fmt(fmt), ##__VA_ARGS__)

struct pt_regs;
void panic(const char *fmt, ...) __noreturn __printf(1, 2);
void die(const char *msg, struct pt_regs *regs, unsigned long esr) __noreturn;
void __bug(const char *file, int line, const char *cond) __noreturn;
void __warn(const char *file, int line, const char *cond);
void dump_stack(void);
void backtrace_from(u64 fp, u64 pc);
const char *ksym_lookup(u64 addr, u64 *offset);
extern volatile int in_panic;

#define BUG() __bug(__FILE__, __LINE__, "BUG")
#define BUG_ON(c)                                    \
  do {                                               \
    if (unlikely(c)) __bug(__FILE__, __LINE__, #c);  \
  } while (0)
#define WARN_ON(c)                                            \
  ({                                                          \
    bool __w = !!(c);                                         \
    if (unlikely(__w)) __warn(__FILE__, __LINE__, #c);        \
    __w;                                                      \
  })
#ifdef CONFIG_DEBUG
#define ASSERT(c) BUG_ON(!(c))
#else
#define ASSERT(c) ((void)0)
#endif

/* Kernel log ring buffer (dmesg). */
size_t klog_read(char *buf, size_t size, size_t *pos);
size_t klog_size(void);

/* Consoles: all registered consoles receive printk output. */
struct console {
  const char *name;
  void (*write)(struct console *c, const char *s, size_t n);
  void *priv;
  bool no_replay; /* takes over from an early console on the same device */
  struct console *next;
};
void register_console(struct console *c);
void unregister_console(struct console *c);
void console_flush_panic(const char *s, size_t n);

/* Kernel command line (from /chosen/bootargs). */
const char *kernel_cmdline(void);
/* Value of a key=value kernel parameter (copied into buf), or NULL. */
const char *cmdline_get(const char *key, char *buf, size_t size);
/* Console selection: console= on the command line overrides stdout-path. */
bool console_selected(const char *ttyname, bool is_stdout_path);
int kernel_init(void *arg);

/* Time since boot. */
u64 ktime_ns(void);

#endif
