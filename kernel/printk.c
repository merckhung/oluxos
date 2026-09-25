/*
 * printk: formats into a line buffer, appends to the kernel log ring buffer
 * (readable through /proc/kmsg and dmesg) and writes to every registered
 * console. Safe to call from any context, including IRQ handlers and early
 * boot before the scheduler exists.
 */
#include <olux/kernel.h>
#include <olux/smp.h>
#include <olux/spinlock.h>

#define KLOG_SIZE (64 * 1024)

static char klog[KLOG_SIZE];
static size_t klog_head; /* total bytes ever written */
static DEFINE_SPINLOCK(printk_lock);
static struct console *consoles;
int console_loglevel = LOGLEVEL_INFO;

void register_console(struct console *c) {
  unsigned long f = spin_lock_irqsave(&printk_lock);
  c->next = consoles;
  consoles = c;
  /* Replay the log so a late console sees the boot messages. */
  size_t start = klog_head;
  if (!c->no_replay) start = klog_head > KLOG_SIZE ? klog_head - KLOG_SIZE : 0;
  for (size_t i = start; i < klog_head;) {
    size_t off = i % KLOG_SIZE;
    size_t n = MIN(klog_head - i, (size_t)KLOG_SIZE - off);
    c->write(c, &klog[off], n);
    i += n;
  }
  spin_unlock_irqrestore(&printk_lock, f);
}

void unregister_console(struct console *c) {
  unsigned long f = spin_lock_irqsave(&printk_lock);
  for (struct console **pp = &consoles; *pp; pp = &(*pp)->next) {
    if (*pp == c) {
      *pp = c->next;
      break;
    }
  }
  spin_unlock_irqrestore(&printk_lock, f);
}

static void klog_append(const char *s, size_t n) {
  for (size_t i = 0; i < n; i++) klog[(klog_head + i) % KLOG_SIZE] = s[i];
  klog_head += n;
}

size_t klog_size(void) { return klog_head; }

/* Copy log bytes starting at *pos (absolute offset) into buf. */
size_t klog_read(char *buf, size_t size, size_t *pos) {
  unsigned long f = spin_lock_irqsave(&printk_lock);
  size_t start = klog_head > KLOG_SIZE ? klog_head - KLOG_SIZE : 0;
  if (*pos < start) *pos = start;
  size_t n = 0;
  while (n < size && *pos < klog_head) buf[n++] = klog[(*pos)++ % KLOG_SIZE];
  spin_unlock_irqrestore(&printk_lock, f);
  return n;
}

void console_flush_panic(const char *s, size_t n) {
  for (struct console *c = consoles; c; c = c->next) c->write(c, s, n);
}

int vprintk(int level, const char *fmt, va_list ap) {
  char line[512];
  u64 ns = ktime_ns();
  int hdr = snprintf(line, sizeof(line), "[%5llu.%06llu] ", (unsigned long long)(ns / 1000000000ULL),
                     (unsigned long long)((ns / 1000) % 1000000));
  int n = vsnprintf(line + hdr, sizeof(line) - hdr, fmt, ap);
  size_t len = MIN((size_t)(hdr + n), sizeof(line) - 1);

  unsigned long f = local_irq_save();
  /* Never deadlock on a CPU that faulted while holding the lock. */
  bool locked = true;
  if (in_panic) locked = spin_trylock(&printk_lock);
  else spin_lock(&printk_lock);
  klog_append(line, len);
  if (level <= console_loglevel)
    for (struct console *c = consoles; c; c = c->next) c->write(c, line, len);
  if (locked) spin_unlock(&printk_lock);
  local_irq_restore(f);
  return n;
}

int printk(int level, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int n = vprintk(level, fmt, ap);
  va_end(ap);
  return n;
}
