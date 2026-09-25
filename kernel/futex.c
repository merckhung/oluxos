/*
 * Fast userspace mutexes. Waiters are keyed by (address space, address) for
 * private futexes and by physical address for shared ones, hashed into
 * wait buckets.
 */
#include <olux/futex.h>
#include <olux/kernel.h>
#include <olux/mm.h>
#include <olux/process.h>
#include <olux/sched.h>
#include <olux/uaccess.h>
#include <olux/vm.h>

#define FUTEX_WAIT 0
#define FUTEX_WAKE 1
#define FUTEX_REQUEUE 3
#define FUTEX_CMP_REQUEUE 4
#define FUTEX_WAKE_OP 5
#define FUTEX_WAIT_BITSET 9
#define FUTEX_WAKE_BITSET 10
#define FUTEX_PRIVATE_FLAG 128
#define FUTEX_CLOCK_REALTIME 256
#define FUTEX_BITSET_MATCH_ANY 0xffffffffU

struct futex_waiter {
  struct thread *t;
  u64 key_a, key_b;
  u32 bitset;
  bool woken;
  struct list_head link;
};

#define NBUCKETS 64
static struct list_head buckets[NBUCKETS];
static DEFINE_SPINLOCK(futex_lock);
static bool futex_ready;

static void futex_init(void) {
  for (int i = 0; i < NBUCKETS; i++) list_init(&buckets[i]);
  futex_ready = true;
}

static int make_key(u64 uaddr, bool private, u64 *a, u64 *b) {
  if (uaddr & 3) return -EINVAL;
  struct mm *mm = current->proc->mm;
  if (private) {
    *a = (u64)mm;
    *b = uaddr;
    return 0;
  }
  void *kp = mm_user_page(mm, uaddr, false);
  if (!kp) return -EFAULT;
  *a = 0;
  *b = virt_to_phys(kp);
  return 0;
}

static unsigned hash(u64 a, u64 b) { return (unsigned)(((a >> 4) ^ (b >> 2) ^ (b >> 12)) % NBUCKETS); }

long futex_wait(u64 uaddr, u32 val, long timeout_ns, u32 bitset, bool private) {
  if (!futex_ready) futex_init();
  if (!bitset) return -EINVAL;
  u64 ka, kb;
  int r = make_key(uaddr, private, &ka, &kb);
  if (r) return r;
  struct futex_waiter w = {.t = current, .key_a = ka, .key_b = kb, .bitset = bitset};
  struct list_head *bk = &buckets[hash(ka, kb)];

  unsigned long f = spin_lock_irqsave(&futex_lock);
  u32 cur;
  if (get_user(cur, uaddr)) {
    spin_unlock_irqrestore(&futex_lock, f);
    return -EFAULT;
  }
  if (cur != val) {
    spin_unlock_irqrestore(&futex_lock, f);
    return -EAGAIN;
  }
  list_add_tail(&w.link, bk);
  current->state = TASK_INTERRUPTIBLE;
  spin_unlock_irqrestore(&futex_lock, f);

  long ret = 0;
  if (!signal_pending_current()) {
    if (timeout_ns >= 0) {
      if (timeout_ns == 0 || schedule_timeout(timeout_ns) == 0) ret = -ETIMEDOUT;
    } else {
      schedule();
    }
  }
  current->state = TASK_RUNNING;
  f = spin_lock_irqsave(&futex_lock);
  if (w.woken) ret = 0;
  else {
    list_del(&w.link);
    if (ret == 0) ret = signal_pending_current() ? -EINTR : 0;
    if (ret == 0 && !w.woken) ret = -EAGAIN; /* spurious: let userspace retry */
  }
  spin_unlock_irqrestore(&futex_lock, f);
  return ret;
}

static int wake_key(u64 ka, u64 kb, int n, u32 bitset) {
  struct list_head *bk = &buckets[hash(ka, kb)];
  struct futex_waiter *w, *tmp;
  int woken = 0;
  list_for_each_entry_safe(w, tmp, bk, link) {
    if (woken >= n) break;
    if (w->key_a != ka || w->key_b != kb || !(w->bitset & bitset)) continue;
    list_del(&w->link);
    list_init(&w->link);
    w->woken = true;
    wake_up_thread(w->t);
    woken++;
  }
  return woken;
}

long futex_wake_bitset(u64 uaddr, int n, u32 bitset, bool private) {
  if (!futex_ready) futex_init();
  if (!bitset) return -EINVAL;
  u64 ka, kb;
  int r = make_key(uaddr, private, &ka, &kb);
  if (r) return r;
  unsigned long f = spin_lock_irqsave(&futex_lock);
  int woken = wake_key(ka, kb, n, bitset);
  spin_unlock_irqrestore(&futex_lock, f);
  return woken;
}

/* Used by thread exit (CLONE_CHILD_CLEARTID): wake both key kinds. */
long futex_wake(u64 uaddr, int n) {
  long a = futex_wake_bitset(uaddr, n, FUTEX_BITSET_MATCH_ANY, true);
  long b = futex_wake_bitset(uaddr, n, FUTEX_BITSET_MATCH_ANY, false);
  return (a > 0 ? a : 0) + (b > 0 ? b : 0);
}

long futex_requeue(u64 uaddr, int nwake, u64 uaddr2, int nrequeue, bool cmp, u32 cmpval, bool private) {
  if (!futex_ready) futex_init();
  u64 ka, kb, ka2, kb2;
  int r = make_key(uaddr, private, &ka, &kb);
  if (r) return r;
  r = make_key(uaddr2, private, &ka2, &kb2);
  if (r) return r;
  unsigned long f = spin_lock_irqsave(&futex_lock);
  if (cmp) {
    u32 cur;
    if (get_user(cur, uaddr)) {
      spin_unlock_irqrestore(&futex_lock, f);
      return -EFAULT;
    }
    if (cur != cmpval) {
      spin_unlock_irqrestore(&futex_lock, f);
      return -EAGAIN;
    }
  }
  int woken = wake_key(ka, kb, nwake, FUTEX_BITSET_MATCH_ANY);
  int moved = 0;
  struct list_head *bk = &buckets[hash(ka, kb)], *bk2 = &buckets[hash(ka2, kb2)];
  struct futex_waiter *w, *tmp;
  list_for_each_entry_safe(w, tmp, bk, link) {
    if (moved >= nrequeue) break;
    if (w->key_a != ka || w->key_b != kb) continue;
    list_del(&w->link);
    w->key_a = ka2;
    w->key_b = kb2;
    list_add_tail(&w->link, bk2);
    moved++;
  }
  spin_unlock_irqrestore(&futex_lock, f);
  return woken + moved;
}

long sys_futex(u64 uaddr, u64 op, u64 val, u64 utime, u64 uaddr2, u64 val3);
long sys_futex(u64 uaddr, u64 op, u64 val, u64 utime, u64 uaddr2, u64 val3) {
  bool private = op & FUTEX_PRIVATE_FLAG;
  int cmd = op & ~(FUTEX_PRIVATE_FLAG | FUTEX_CLOCK_REALTIME);
  long timeout = -1;
  if ((cmd == FUTEX_WAIT || cmd == FUTEX_WAIT_BITSET) && utime) {
    struct timespec64 ts;
    if (copy_from_user(&ts, utime, sizeof(ts))) return -EFAULT;
    if (ts.tv_sec < 0 || ts.tv_nsec < 0 || ts.tv_nsec >= (s64)NSEC_PER_SEC) return -EINVAL;
    s64 ns = ts.tv_sec > 1000000000 ? (1L << 62) : ts.tv_sec * (s64)NSEC_PER_SEC + ts.tv_nsec;
    if (cmd == FUTEX_WAIT_BITSET) {
      /* absolute timeout */
      s64 now = (op & FUTEX_CLOCK_REALTIME) ? (s64)ktime_realtime_ns() : (s64)ktime_ns();
      ns = ns > now ? ns - now : 0;
    }
    timeout = ns;
  }
  switch (cmd) {
    case FUTEX_WAIT:
      return futex_wait(uaddr, (u32)val, timeout, FUTEX_BITSET_MATCH_ANY, private);
    case FUTEX_WAIT_BITSET:
      return futex_wait(uaddr, (u32)val, timeout, (u32)val3, private);
    case FUTEX_WAKE:
      return futex_wake_bitset(uaddr, (int)val, FUTEX_BITSET_MATCH_ANY, private);
    case FUTEX_WAKE_BITSET:
      return futex_wake_bitset(uaddr, (int)val, (u32)val3, private);
    case FUTEX_REQUEUE:
      return futex_requeue(uaddr, (int)val, uaddr2, (int)utime, false, 0, private);
    case FUTEX_CMP_REQUEUE:
      return futex_requeue(uaddr, (int)val, uaddr2, (int)utime, true, (u32)val3, private);
    default:
      return -ENOSYS;
  }
}
