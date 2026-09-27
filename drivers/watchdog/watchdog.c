/* Watchdog core and /dev/watchdog (see include/olux/watchdog.h). */
#include <olux/device.h>
#include <olux/fs.h>
#include <olux/kernel.h>
#include <olux/pstore.h>
#include <olux/reboot.h>
#include <olux/spinlock.h>
#include <olux/time.h>
#include <olux/uaccess.h>
#include <olux/watchdog.h>

#define DEFAULT_TIMEOUT 60
#define MAX_TIMEOUT 3600

#define WDIOC_GETSUPPORT 0x80285700u
#define WDIOC_GETSTATUS 0x80045701u
#define WDIOC_GETBOOTSTATUS 0x80045702u
#define WDIOC_SETOPTIONS 0x80045704u
#define WDIOC_KEEPALIVE 0x80045705u
#define WDIOC_SETTIMEOUT 0xC0045706u
#define WDIOC_GETTIMEOUT 0x80045707u
#define WDIOC_GETTIMELEFT 0x8004570Au
#define WDIOS_DISABLECARD 1
#define WDIOS_ENABLECARD 2
#define WDIOF_SETTIMEOUT 0x0080
#define WDIOF_MAGICCLOSE 0x0100
#define WDIOF_KEEPALIVEPING 0x8000
#define WDIOF_CARDRESET 0x0020

struct watchdog_info {
  u32 options;
  u32 firmware_version;
  u8 identity[32];
};

static struct {
  struct watchdog_device *wd;
  spinlock_t lock;
  bool open, running, magic;
  unsigned timeout;
  u64 deadline;
  struct ktimer timer;
} W = {.lock = SPINLOCK_INIT, .timeout = DEFAULT_TIMEOUT};

static u64 hw_period_ns(void) {
  unsigned hw = W.wd->max_hw_timeout;
  return hw ? (u64)hw * NSEC_PER_SEC / 2 : NSEC_PER_SEC;
}

/* Kernel heartbeat: keep the hardware alive until the user deadline. */
static void heartbeat(struct ktimer *t) {
  u64 now = ktime_ns();
  if (!W.running) return;
  if (now >= W.deadline) {
    pstore_set_state(PSTORE_WATCHDOG);
    if (!W.wd->max_hw_timeout) {
      pr_emerg("watchdog: %s: no keepalive for %u s; restarting\n", W.wd->name, W.timeout);
      machine_restart();
    }
    pr_emerg("watchdog: %s: no keepalive for %u s; hardware reset imminent\n", W.wd->name, W.timeout);
    return; /* stop pinging: the hardware resets the board */
  }
  if (W.wd->ping) W.wd->ping(W.wd);
  ktimer_start(&W.timer, now + MIN(hw_period_ns(), W.deadline - now));
}

static int wd_start(void) {
  unsigned hw = W.wd->max_hw_timeout;
  int r = W.wd->start ? W.wd->start(W.wd, hw && W.timeout > hw ? hw : W.timeout) : 0;
  if (r) return r;
  W.deadline = ktime_ns() + (u64)W.timeout * NSEC_PER_SEC;
  W.running = true;
  ktimer_start(&W.timer, ktime_ns() + MIN(hw_period_ns(), (u64)W.timeout * NSEC_PER_SEC));
  return 0;
}

static void wd_stop(void) {
  W.running = false;
  ktimer_cancel(&W.timer);
  if (W.wd->stop) W.wd->stop(W.wd);
}

static void wd_keepalive(void) {
  unsigned long f = spin_lock_irqsave(&W.lock);
  W.deadline = ktime_ns() + (u64)W.timeout * NSEC_PER_SEC;
  spin_unlock_irqrestore(&W.lock, f);
  if (W.wd->ping) W.wd->ping(W.wd);
}

static int wdev_open(struct inode *i, struct file *f) {
  if (!W.wd) return -ENODEV;
  if (W.open) return -EBUSY;
  if (!W.running) {
    int r = wd_start();
    if (r) return r;
  } else {
    wd_keepalive();
  }
  W.open = true;
  W.magic = false;
  return 0;
}

static int wdev_release(struct inode *i, struct file *f) {
  if (W.magic) {
    wd_stop();
  } else if (W.running) {
    pr_err("watchdog: %s: device closed unexpectedly; watchdog keeps running\n", W.wd->name);
  }
  W.open = false;
  return 0;
}

static ssize_t wdev_write(struct file *f, struct iobuf *b, loff_t *pos) {
  W.magic = false;
  for (size_t i = 0; i < b->len && i < 64; i++) {
    char c;
    if (iob_read(b, i, &c, 1)) return -EFAULT;
    if (c == 'V') W.magic = true;
  }
  wd_keepalive();
  return b->len;
}

static long wdev_ioctl(struct file *f, unsigned cmd, u64 arg) {
  int v = 0;
  switch (cmd) {
    case WDIOC_GETSUPPORT: {
      struct watchdog_info wi = {WDIOF_SETTIMEOUT | WDIOF_MAGICCLOSE | WDIOF_KEEPALIVEPING, 0, {0}};
      strlcpy((char *)wi.identity, W.wd->name, sizeof(wi.identity));
      return copy_to_user(arg, &wi, sizeof(wi));
    }
    case WDIOC_GETSTATUS:
      return put_user(0, (int *)arg);
    case WDIOC_GETBOOTSTATUS:
      return put_user(!strcmp(last_reset_reason, "watchdog") ? WDIOF_CARDRESET : 0, (int *)arg);
    case WDIOC_KEEPALIVE:
      wd_keepalive();
      return 0;
    case WDIOC_SETTIMEOUT:
      if (get_user(v, (int *)arg)) return -EFAULT;
      if (v < 1 || v > MAX_TIMEOUT) return -EINVAL;
      W.timeout = (unsigned)v;
      if (W.running) {
        wd_stop();
        wd_start();
      }
      return put_user(v, (int *)arg);
    case WDIOC_GETTIMEOUT:
      return put_user((int)W.timeout, (int *)arg);
    case WDIOC_GETTIMELEFT: {
      u64 now = ktime_ns();
      return put_user(W.running && W.deadline > now ? (int)((W.deadline - now) / NSEC_PER_SEC) : 0, (int *)arg);
    }
    case WDIOC_SETOPTIONS:
      if (get_user(v, (int *)arg)) return -EFAULT;
      if (v & WDIOS_DISABLECARD) wd_stop();
      if ((v & WDIOS_ENABLECARD) && !W.running) return wd_start();
      return 0;
    default:
      return -ENOTTY;
  }
}

static const struct file_operations wdev_fops = {
    .open = wdev_open, .release = wdev_release, .write = wdev_write, .ioctl = wdev_ioctl};

int watchdog_register(struct watchdog_device *wd) {
  if (W.wd) return -EBUSY;
  W.wd = wd;
  ktimer_init(&W.timer, heartbeat, NULL);
  register_chrdev(MKDEV(10, 130), "watchdog", &wdev_fops, NULL);
  devfs_create("watchdog", S_IFCHR | 0600, MKDEV(10, 130));
  pr_info("watchdog: %s registered (default timeout %u s)\n", wd->name, W.timeout);
  return 0;
}

/* Software fallback when the platform has no hardware watchdog: catches a
 * hung userspace supervisor, not a hung kernel. */
static struct watchdog_device softdog = {.name = "softdog"};

static int softdog_init(void) {
  if (!W.wd) watchdog_register(&softdog);
  return 0;
}
late_initcall(softdog_init);
