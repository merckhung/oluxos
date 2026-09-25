/* Process, credential, scheduling, time and system information syscalls. */
#include <olux/fs.h>
#include <olux/kernel.h>
#include <olux/mm.h>
#include <olux/process.h>
#include <olux/reboot.h>
#include <olux/sched.h>
#include <olux/smp.h>
#include <olux/tty.h>
#include <olux/uaccess.h>
#include <olux/vm.h>

static char hostname[65] = "oluxos";
static char domainname[65] = "(none)";

/* ---------------- process lifecycle ---------------- */

long sys_exit(u64 code);
long sys_exit(u64 code) { do_exit((int)code); }

long sys_exit_group(u64 code);
long sys_exit_group(u64 code) { do_group_exit(((int)code & 0xff) << 8); }

long sys_clone(u64 flags, u64 newsp, u64 ptid, u64 tls, u64 ctid);
long sys_clone(u64 flags, u64 newsp, u64 ptid, u64 tls, u64 ctid) {
  return do_fork(flags, newsp, ptid, tls, ctid, current->user_regs);
}

long sys_clone3(u64 args, u64 size);
long sys_clone3(u64 args, u64 size) { return -ENOSYS; }

long sys_wait4(u64 pid, u64 status, u64 options, u64 rusage);
long sys_wait4(u64 pid, u64 status, u64 options, u64 rusage) {
  long r = do_wait4((int)pid, status, (int)options, rusage);
  return r == -ERESTARTSYS ? -ERESTARTSYS : r;
}

long sys_waitid(u64 idtype, u64 id, u64 uinfo, u64 options, u64 rusage);
long sys_waitid(u64 idtype, u64 id, u64 uinfo, u64 options, u64 rusage) {
  int pid = idtype == 0 /* P_ALL */ ? -1 : idtype == 1 /* P_PID */ ? (int)id : idtype == 2 ? -(int)id : 0;
  if (idtype > 2) return -EINVAL;
  s32 status = 0;
  long r = do_wait4(pid, 0, (int)(options | 2), rusage);
  (void)status;
  if (r < 0) return r;
  if (uinfo) {
    siginfo_t si = {0};
    if (r > 0) {
      si.si_signo = SIGCHLD;
      si.si_code = CLD_EXITED;
      si.chld.pid = (s32)r;
    }
    if (copy_to_user(uinfo, &si, sizeof(si))) return -EFAULT;
  }
  return 0;
}

long sys_set_tid_address(u64 tidptr);
long sys_set_tid_address(u64 tidptr) {
  current->clear_child_tid = tidptr;
  return current->tid;
}

long sys_set_robust_list(u64 head, u64 len);
long sys_set_robust_list(u64 head, u64 len) {
  if (len != 24) return -EINVAL;
  current->robust_list = head;
  return 0;
}

long sys_get_robust_list(u64 pid, u64 headp, u64 lenp);
long sys_get_robust_list(u64 pid, u64 headp, u64 lenp) {
  if (put_user(current->robust_list, headp) || put_user((u64)24, lenp)) return -EFAULT;
  return 0;
}

long sys_rseq(void);
long sys_rseq(void) { return -ENOSYS; }

/* ---------------- ids ---------------- */

long sys_getpid(void);
long sys_getpid(void) { return current->proc->pid; }
long sys_gettid(void);
long sys_gettid(void) { return current->tid; }
long sys_getppid(void);
long sys_getppid(void) { return current->proc->parent ? current->proc->parent->pid : 0; }
long sys_getuid(void);
long sys_getuid(void) { return current->proc->cred.uid; }
long sys_geteuid(void);
long sys_geteuid(void) { return current->proc->cred.euid; }
long sys_getgid(void);
long sys_getgid(void) { return current->proc->cred.gid; }
long sys_getegid(void);
long sys_getegid(void) { return current->proc->cred.egid; }

static bool uid_ok(struct cred *c, u32 id) { return c->euid == 0 || id == c->uid || id == c->euid || id == c->suid; }
static bool gid_ok(struct cred *c, u32 id) { return c->euid == 0 || id == c->gid || id == c->egid || id == c->sgid; }

long sys_setresuid(u64 r, u64 e, u64 s);
long sys_setresuid(u64 r, u64 e, u64 s) {
  struct cred *c = &current->proc->cred;
  if (((u32)r != ~0U && !uid_ok(c, r)) || ((u32)e != ~0U && !uid_ok(c, e)) || ((u32)s != ~0U && !uid_ok(c, s)))
    return -EPERM;
  if ((u32)r != ~0U) c->uid = r;
  if ((u32)e != ~0U) c->euid = e;
  if ((u32)s != ~0U) c->suid = s;
  return 0;
}

long sys_setresgid(u64 r, u64 e, u64 s);
long sys_setresgid(u64 r, u64 e, u64 s) {
  struct cred *c = &current->proc->cred;
  if (((u32)r != ~0U && !gid_ok(c, r)) || ((u32)e != ~0U && !gid_ok(c, e)) || ((u32)s != ~0U && !gid_ok(c, s)))
    return -EPERM;
  if ((u32)r != ~0U) c->gid = r;
  if ((u32)e != ~0U) c->egid = e;
  if ((u32)s != ~0U) c->sgid = s;
  return 0;
}

long sys_setuid(u64 uid);
long sys_setuid(u64 uid) {
  struct cred *c = &current->proc->cred;
  if (c->euid == 0) {
    c->uid = c->euid = c->suid = uid;
    return 0;
  }
  if (uid != c->uid && uid != c->suid) return -EPERM;
  c->euid = uid;
  return 0;
}

long sys_setgid(u64 gid);
long sys_setgid(u64 gid) {
  struct cred *c = &current->proc->cred;
  if (c->euid == 0) {
    c->gid = c->egid = c->sgid = gid;
    return 0;
  }
  if (gid != c->gid && gid != c->sgid) return -EPERM;
  c->egid = gid;
  return 0;
}

long sys_setreuid(u64 r, u64 e);
long sys_setreuid(u64 r, u64 e) { return sys_setresuid(r, e, ~0U); }
long sys_setregid(u64 r, u64 e);
long sys_setregid(u64 r, u64 e) { return sys_setresgid(r, e, ~0U); }
long sys_setfsuid(u64 id);
long sys_setfsuid(u64 id) { return current->proc->cred.euid; }
long sys_setfsgid(u64 id);
long sys_setfsgid(u64 id) { return current->proc->cred.egid; }

long sys_getresuid(u64 r, u64 e, u64 s);
long sys_getresuid(u64 r, u64 e, u64 s) {
  struct cred *c = &current->proc->cred;
  if (put_user(c->uid, r) || put_user(c->euid, e) || put_user(c->suid, s)) return -EFAULT;
  return 0;
}

long sys_getresgid(u64 r, u64 e, u64 s);
long sys_getresgid(u64 r, u64 e, u64 s) {
  struct cred *c = &current->proc->cred;
  if (put_user(c->gid, r) || put_user(c->egid, e) || put_user(c->sgid, s)) return -EFAULT;
  return 0;
}

long sys_getgroups(u64 size, u64 list);
long sys_getgroups(u64 size, u64 list) {
  struct cred *c = &current->proc->cred;
  if (size == 0) return c->ngroups;
  if ((int)size < c->ngroups) return -EINVAL;
  return copy_to_user(list, c->groups, c->ngroups * sizeof(gid_t)) ? -EFAULT : c->ngroups;
}

long sys_setgroups(u64 size, u64 list);
long sys_setgroups(u64 size, u64 list) {
  struct cred *c = &current->proc->cred;
  if (c->euid != 0) return -EPERM;
  if (size > ARRAY_SIZE(c->groups)) return -EINVAL;
  if (copy_from_user(c->groups, list, size * sizeof(gid_t))) return -EFAULT;
  c->ngroups = (int)size;
  return 0;
}

long sys_capget(u64 hdr, u64 data);
long sys_capget(u64 hdr, u64 data) {
  if (!data) return 0;
  u32 caps[6] = {0};
  if (current->proc->cred.euid == 0) caps[0] = caps[1] = caps[3] = caps[4] = ~0U;
  return copy_to_user(data, caps, sizeof(caps));
}

long sys_capset(u64 hdr, u64 data);
long sys_capset(u64 hdr, u64 data) { return capable_root() ? 0 : -EPERM; }

/* ---------------- sessions / process groups ---------------- */

long sys_setpgid(u64 upid, u64 upgid);
long sys_setpgid(u64 upid, u64 upgid) {
  struct process *self = current->proc;
  int pid = (int)upid ? (int)upid : self->pid;
  int pgid = (int)upgid ? (int)upgid : pid;
  if (pgid < 0) return -EINVAL;
  struct process *p = process_find(pid);
  if (!p || (p != self && p->parent != self)) return -ESRCH;
  if (p->sid == p->pid) return -EPERM; /* session leader */
  if (p->sid != self->sid) return -EPERM;
  if (pgid != pid) {
    bool found = false;
    struct process *q;
    list_for_each_entry(q, &all_processes, all_link)
      if (q->pgid == pgid && q->sid == self->sid) found = true;
    if (!found) return -EPERM;
  }
  p->pgid = pgid;
  return 0;
}

long sys_getpgid(u64 pid);
long sys_getpgid(u64 pid) {
  struct process *p = pid ? process_find((int)pid) : current->proc;
  return p ? p->pgid : -ESRCH;
}

long sys_getsid(u64 pid);
long sys_getsid(u64 pid) {
  struct process *p = pid ? process_find((int)pid) : current->proc;
  return p ? p->sid : -ESRCH;
}

long sys_setsid(void);
long sys_setsid(void) {
  struct process *p = current->proc;
  struct process *q;
  list_for_each_entry(q, &all_processes, all_link)
    if (q->pgid == p->pid) return -EPERM;
  p->sid = p->pgid = p->pid;
  p->tty = NULL;
  return p->sid;
}

/* ---------------- system information ---------------- */

long sys_uname(u64 buf);
long sys_uname(u64 buf) {
  struct {
    char sysname[65], nodename[65], release[65], version[65], machine[65], domainname[65];
  } u;
  memset(&u, 0, sizeof(u));
  strlcpy(u.sysname, "OluxOS", 65);
  strlcpy(u.nodename, hostname, 65);
  strlcpy(u.release, OLUX_VERSION, 65);
  snprintf(u.version, 65, "#1 SMP %s", OLUX_GITREV);
  strlcpy(u.machine, "aarch64", 65);
  strlcpy(u.domainname, domainname, 65);
  return copy_to_user(buf, &u, sizeof(u));
}

long sys_sethostname(u64 name, u64 len);
long sys_sethostname(u64 name, u64 len) {
  if (!capable_root()) return -EPERM;
  if (len > 64) return -EINVAL;
  char tmp[65] = {0};
  if (copy_from_user(tmp, name, len)) return -EFAULT;
  memcpy(hostname, tmp, sizeof(hostname));
  return 0;
}

long sys_setdomainname(u64 name, u64 len);
long sys_setdomainname(u64 name, u64 len) {
  if (!capable_root()) return -EPERM;
  if (len > 64) return -EINVAL;
  char tmp[65] = {0};
  if (copy_from_user(tmp, name, len)) return -EFAULT;
  memcpy(domainname, tmp, sizeof(domainname));
  return 0;
}

const char *get_hostname(void);
const char *get_hostname(void) { return hostname; }

long sys_sysinfo(u64 buf);
long sys_sysinfo(u64 buf) {
  struct {
    s64 uptime;
    u64 loads[3];
    u64 totalram, freeram, sharedram, bufferram, totalswap, freeswap;
    u16 procs, pad;
    u64 totalhigh, freehigh;
    u32 mem_unit;
    char pad2[256];
  } si;
  memset(&si, 0, sizeof(si));
  si.uptime = ktime_ns() / NSEC_PER_SEC;
  extern u64 load_avg[3];
  memcpy(si.loads, load_avg, sizeof(si.loads));
  si.totalram = nr_total_pages() * PAGE_SIZE;
  si.freeram = nr_free_pages() * PAGE_SIZE;
  int n = 0;
  struct process *p;
  list_for_each_entry(p, &all_processes, all_link) n++;
  si.procs = n;
  si.mem_unit = 1;
  return copy_to_user(buf, &si, 112);
}

long sys_getcpu(u64 cpu, u64 node, u64 cache);
long sys_getcpu(u64 cpu, u64 node, u64 cache) {
  if (cpu && put_user((u32)smp_processor_id(), cpu)) return -EFAULT;
  if (node && put_user((u32)0, node)) return -EFAULT;
  return 0;
}

long sys_umask(u64 mask);
long sys_umask(u64 mask) {
  mode_t old = current->proc->umask;
  current->proc->umask = mask & 0777;
  return old;
}

#define PR_SET_PDEATHSIG 1
#define PR_GET_DUMPABLE 3
#define PR_SET_DUMPABLE 4
#define PR_SET_NAME 15
#define PR_GET_NAME 16
#define PR_SET_NO_NEW_PRIVS 38

long sys_prctl(u64 opt, u64 a2, u64 a3, u64 a4, u64 a5);
long sys_prctl(u64 opt, u64 a2, u64 a3, u64 a4, u64 a5) {
  switch (opt) {
    case PR_SET_NAME: {
      char name[16] = {0};
      long n = strncpy_from_user(name, a2, sizeof(name));
      if (n == -EFAULT) return -EFAULT;
      name[15] = '\0';
      strlcpy(current->name, name, sizeof(current->name));
      return 0;
    }
    case PR_GET_NAME:
      return copy_to_user(a2, current->name, 16);
    case PR_SET_PDEATHSIG:
    case PR_SET_DUMPABLE:
    case PR_SET_NO_NEW_PRIVS:
      return 0;
    case PR_GET_DUMPABLE:
      return 1;
    default:
      return -EINVAL;
  }
}

/* ---------------- resource limits / usage ---------------- */

long sys_prlimit64(u64 pid, u64 res, u64 unew, u64 uold);
long sys_prlimit64(u64 pid, u64 res, u64 unew, u64 uold) {
  if (res >= RLIM_NLIMITS) return -EINVAL;
  struct process *p = pid ? process_find((int)pid) : current->proc;
  if (!p) return -ESRCH;
  struct rlimit64 old = p->rlim[res];
  if (unew) {
    struct rlimit64 n;
    if (copy_from_user(&n, unew, sizeof(n))) return -EFAULT;
    if (n.cur > n.max) return -EINVAL;
    if (n.max > p->rlim[res].max && !capable_root()) return -EPERM;
    if (res == RLIMIT_NOFILE && n.max > NR_OPEN_MAX) return -EPERM;
    p->rlim[res] = n;
  }
  if (uold && copy_to_user(uold, &old, sizeof(old))) return -EFAULT;
  return 0;
}

long sys_getrlimit(u64 res, u64 u);
long sys_getrlimit(u64 res, u64 u) { return sys_prlimit64(0, res, 0, u); }
long sys_setrlimit(u64 res, u64 u);
long sys_setrlimit(u64 res, u64 u) { return sys_prlimit64(0, res, u, 0); }

long sys_getrusage(u64 who, u64 u);
long sys_getrusage(u64 who, u64 u) {
  struct process *p = current->proc;
  u64 ut = p->utime, st = p->stime;
  struct thread *t;
  list_for_each_entry(t, &p->threads, thread_link) {
    ut += t->utime;
    st += t->stime;
  }
  if ((s64)who == -1) {
    ut = p->cutime;
    st = p->cstime;
  }
  s64 ru[18] = {0};
  ru[0] = ut / NSEC_PER_SEC;
  ru[1] = ut % NSEC_PER_SEC / 1000;
  ru[2] = st / NSEC_PER_SEC;
  ru[3] = st % NSEC_PER_SEC / 1000;
  ru[4] = (p->mm ? p->mm->rss_pages : 0) * (PAGE_SIZE / 1024); /* maxrss (KiB) */
  ru[8] = p->min_flt;
  return copy_to_user(u, ru, sizeof(ru));
}

long sys_times(u64 buf);
long sys_times(u64 buf) {
  struct process *p = current->proc;
  u64 ut = p->utime, st = p->stime;
  struct thread *t;
  list_for_each_entry(t, &p->threads, thread_link) {
    ut += t->utime;
    st += t->stime;
  }
  const u64 tick = NSEC_PER_SEC / 100; /* USER_HZ */
  u64 tms[4] = {ut / tick, st / tick, p->cutime / tick, p->cstime / tick};
  if (buf && copy_to_user(buf, tms, sizeof(tms))) return -EFAULT;
  return (long)(ktime_ns() / tick);
}

/* ---------------- scheduling ---------------- */

static struct thread *sched_target(u64 pid) {
  if (!pid) return current;
  return thread_find((int)pid);
}

long sys_sched_yield(void);
long sys_sched_yield(void) {
  yield();
  return 0;
}

long sys_sched_setscheduler(u64 pid, u64 policy, u64 uparam);
long sys_sched_setscheduler(u64 pid, u64 policy, u64 uparam) {
  struct thread *t = sched_target(pid);
  if (!t) return -ESRCH;
  s32 prio;
  if (get_user(prio, uparam)) return -EFAULT;
  if ((policy == SCHED_FIFO || policy == SCHED_RR) && !capable_root()) return -EPERM;
  return sched_setscheduler(t, (int)(policy & ~0x40000000), prio);
}

long sys_sched_setparam(u64 pid, u64 uparam);
long sys_sched_setparam(u64 pid, u64 uparam) {
  struct thread *t = sched_target(pid);
  if (!t) return -ESRCH;
  s32 prio;
  if (get_user(prio, uparam)) return -EFAULT;
  return sched_setscheduler(t, t->policy, prio);
}

long sys_sched_getscheduler(u64 pid);
long sys_sched_getscheduler(u64 pid) {
  struct thread *t = sched_target(pid);
  return t ? t->policy : -ESRCH;
}

long sys_sched_getparam(u64 pid, u64 uparam);
long sys_sched_getparam(u64 pid, u64 uparam) {
  struct thread *t = sched_target(pid);
  if (!t) return -ESRCH;
  return put_user((s32)t->rt_priority, uparam);
}

long sys_sched_get_priority_max(u64 policy);
long sys_sched_get_priority_max(u64 policy) { return (policy == SCHED_FIFO || policy == SCHED_RR) ? 99 : 0; }
long sys_sched_get_priority_min(u64 policy);
long sys_sched_get_priority_min(u64 policy) { return (policy == SCHED_FIFO || policy == SCHED_RR) ? 1 : 0; }

long sys_sched_rr_get_interval(u64 pid, u64 uts);
long sys_sched_rr_get_interval(u64 pid, u64 uts) {
  struct timespec64 ts = {0, (s64)(RR_TIMESLICE_TICKS * TICK_NSEC)};
  return copy_to_user(uts, &ts, sizeof(ts));
}

long sys_sched_setaffinity(u64 pid, u64 len, u64 umask);
long sys_sched_setaffinity(u64 pid, u64 len, u64 umask) {
  struct thread *t = sched_target(pid);
  if (!t) return -ESRCH;
  u64 mask = 0;
  if (copy_from_user(&mask, umask, MIN(len, (u64)8))) return -EFAULT;
  u64 online = 0;
  for (int i = 0; i < nr_cpus_possible; i++)
    if (cpus[i].online) online |= 1UL << i;
  if (!(mask & online)) return -EINVAL;
  t->cpus_allowed = mask & online;
  if (t == current && !(mask & (1UL << smp_processor_id()))) yield();
  return 0;
}

long sys_sched_getaffinity(u64 pid, u64 len, u64 umask);
long sys_sched_getaffinity(u64 pid, u64 len, u64 umask) {
  struct thread *t = sched_target(pid);
  if (!t) return -ESRCH;
  if (len < 8) return -EINVAL;
  u64 online = 0;
  for (int i = 0; i < nr_cpus_possible; i++)
    if (cpus[i].online) online |= 1UL << i;
  u64 mask = t->cpus_allowed & online;
  if (copy_to_user(umask, &mask, 8)) return -EFAULT;
  return 8;
}

long sys_getpriority(u64 which, u64 who);
long sys_getpriority(u64 which, u64 who) {
  if (which != 0) return -EINVAL;
  struct thread *t = sched_target(who);
  if (!t) return -ESRCH;
  return 20 - t->nice;
}

long sys_setpriority(u64 which, u64 who, u64 prio);
long sys_setpriority(u64 which, u64 who, u64 prio) {
  if (which != 0) return -EINVAL;
  struct thread *t = sched_target(who);
  if (!t) return -ESRCH;
  int nice = CLAMP((int)prio, -20, 19);
  if (nice < t->nice && !capable_root()) return -EACCES;
  sched_set_nice(t, nice);
  return 0;
}

/* ---------------- time ---------------- */

#define CLOCK_REALTIME 0
#define CLOCK_MONOTONIC 1
#define CLOCK_PROCESS_CPUTIME_ID 2
#define CLOCK_THREAD_CPUTIME_ID 3
#define CLOCK_MONOTONIC_RAW 4
#define CLOCK_REALTIME_COARSE 5
#define CLOCK_MONOTONIC_COARSE 6
#define CLOCK_BOOTTIME 7
#define TIMER_ABSTIME 1

static int clock_read(u64 id, u64 *ns) {
  switch (id) {
    case CLOCK_REALTIME:
    case CLOCK_REALTIME_COARSE:
      *ns = ktime_realtime_ns();
      return 0;
    case CLOCK_MONOTONIC:
    case CLOCK_MONOTONIC_RAW:
    case CLOCK_MONOTONIC_COARSE:
    case CLOCK_BOOTTIME:
      *ns = ktime_ns();
      return 0;
    case CLOCK_PROCESS_CPUTIME_ID: {
      struct process *p = current->proc;
      u64 t = p->utime + p->stime;
      struct thread *th;
      list_for_each_entry(th, &p->threads, thread_link) t += th->utime + th->stime;
      *ns = t;
      return 0;
    }
    case CLOCK_THREAD_CPUTIME_ID:
      *ns = current->utime + current->stime;
      return 0;
    default:
      return -EINVAL;
  }
}

long sys_clock_gettime(u64 id, u64 uts);
long sys_clock_gettime(u64 id, u64 uts) {
  u64 ns;
  int r = clock_read(id, &ns);
  if (r) return r;
  struct timespec64 ts = {(s64)(ns / NSEC_PER_SEC), (s64)(ns % NSEC_PER_SEC)};
  return copy_to_user(uts, &ts, sizeof(ts));
}

long sys_clock_getres(u64 id, u64 uts);
long sys_clock_getres(u64 id, u64 uts) {
  u64 ns;
  if (clock_read(id, &ns)) return -EINVAL;
  struct timespec64 ts = {0, 1};
  if (id == CLOCK_REALTIME_COARSE || id == CLOCK_MONOTONIC_COARSE) ts.tv_nsec = TICK_NSEC;
  return uts ? copy_to_user(uts, &ts, sizeof(ts)) : 0;
}

static long do_nanosleep(u64 id, bool abs, u64 ureq, u64 urem) {
  struct timespec64 ts;
  if (copy_from_user(&ts, ureq, sizeof(ts))) return -EFAULT;
  if (ts.tv_sec < 0 || ts.tv_nsec < 0 || ts.tv_nsec >= (s64)NSEC_PER_SEC) return -EINVAL;
  u64 now;
  if (clock_read(id, &now)) return -EINVAL;
  u64 dur = ts.tv_sec > 100000000000LL ? ~0ULL / 2 : (u64)ts.tv_sec * NSEC_PER_SEC + ts.tv_nsec;
  if (abs) dur = dur > now ? dur - now : 0;
  if (!dur) return 0;
  u64 end = ktime_ns() + dur;
  while (ktime_ns() < end) {
    if (signal_pending_current()) {
      if (urem && !abs) {
        u64 left = end - ktime_ns();
        struct timespec64 rem = {(s64)(left / NSEC_PER_SEC), (s64)(left % NSEC_PER_SEC)};
        copy_to_user(urem, &rem, sizeof(rem));
      }
      return -EINTR;
    }
    current->state = TASK_INTERRUPTIBLE;
    schedule_timeout(end - ktime_ns() > (u64)1 << 62 ? (long)1 << 62 : (long)(end - ktime_ns()));
    current->state = TASK_RUNNING;
  }
  return 0;
}

long sys_nanosleep(u64 req, u64 rem);
long sys_nanosleep(u64 req, u64 rem) { return do_nanosleep(CLOCK_MONOTONIC, false, req, rem); }

long sys_clock_nanosleep(u64 id, u64 flags, u64 req, u64 rem);
long sys_clock_nanosleep(u64 id, u64 flags, u64 req, u64 rem) {
  if (id == CLOCK_THREAD_CPUTIME_ID || id == CLOCK_PROCESS_CPUTIME_ID) return -EINVAL;
  return do_nanosleep(id, flags & TIMER_ABSTIME, req, rem);
}

long sys_gettimeofday(u64 utv, u64 utz);
long sys_gettimeofday(u64 utv, u64 utz) {
  u64 ns = ktime_realtime_ns();
  if (utv) {
    s64 tv[2] = {(s64)(ns / NSEC_PER_SEC), (s64)(ns % NSEC_PER_SEC / 1000)};
    if (copy_to_user(utv, tv, sizeof(tv))) return -EFAULT;
  }
  if (utz) {
    s32 tz[2] = {0, 0};
    if (copy_to_user(utz, tz, sizeof(tz))) return -EFAULT;
  }
  return 0;
}

void rtc_set_time(u64 ns);
long sys_settimeofday(u64 utv, u64 utz);
long sys_settimeofday(u64 utv, u64 utz) {
  if (!capable_root()) return -EPERM;
  if (!utv) return 0;
  s64 tv[2];
  if (copy_from_user(tv, utv, sizeof(tv))) return -EFAULT;
  if (tv[0] < 0 || tv[1] < 0 || tv[1] >= 1000000) return -EINVAL;
  u64 ns = (u64)tv[0] * NSEC_PER_SEC + tv[1] * 1000;
  set_realtime_ns(ns);
  rtc_set_time(ns);
  return 0;
}

/* ITIMER_REAL (alarm) */
static void alarm_fire(struct ktimer *t) {
  struct process *p = t->arg;
  send_signal_process(p, SIGALRM, NULL);
  if (p->alarm_interval) {
    p->alarm_expires = ktime_ns() + p->alarm_interval;
    ktimer_start(&p->alarm_timer, p->alarm_expires);
  } else {
    p->alarm_expires = 0;
  }
}

struct itimerval {
  s64 int_sec, int_usec, val_sec, val_usec;
};

static void get_itimer(struct process *p, struct itimerval *v) {
  u64 now = ktime_ns();
  u64 left = p->alarm_expires > now ? p->alarm_expires - now : 0;
  v->int_sec = p->alarm_interval / NSEC_PER_SEC;
  v->int_usec = p->alarm_interval % NSEC_PER_SEC / 1000;
  v->val_sec = left / NSEC_PER_SEC;
  v->val_usec = left % NSEC_PER_SEC / 1000;
  if (p->alarm_expires && !v->val_sec && !v->val_usec) v->val_usec = 1;
}

long sys_getitimer(u64 which, u64 uval);
long sys_getitimer(u64 which, u64 uval) {
  if (which != 0) return -EINVAL;
  struct itimerval v;
  get_itimer(current->proc, &v);
  return copy_to_user(uval, &v, sizeof(v));
}

long sys_setitimer(u64 which, u64 unew, u64 uold);
long sys_setitimer(u64 which, u64 unew, u64 uold) {
  if (which != 0) return -EINVAL; /* only ITIMER_REAL */
  struct process *p = current->proc;
  struct itimerval old, n = {0};
  get_itimer(p, &old);
  if (unew && copy_from_user(&n, unew, sizeof(n))) return -EFAULT;
  if (n.val_usec < 0 || n.val_usec >= 1000000 || n.int_usec < 0 || n.int_usec >= 1000000) return -EINVAL;
  ktimer_cancel(&p->alarm_timer);
  p->alarm_expires = 0;
  p->alarm_interval = n.int_sec * NSEC_PER_SEC + n.int_usec * 1000;
  u64 val = n.val_sec * NSEC_PER_SEC + n.val_usec * 1000;
  if (val) {
    ktimer_init(&p->alarm_timer, alarm_fire, p);
    p->alarm_expires = ktime_ns() + val;
    ktimer_start(&p->alarm_timer, p->alarm_expires);
  }
  if (uold && copy_to_user(uold, &old, sizeof(old))) return -EFAULT;
  return 0;
}

/* ---------------- misc ---------------- */

#define LINUX_REBOOT_MAGIC1 0xfee1dead
#define LINUX_REBOOT_CMD_RESTART 0x01234567
#define LINUX_REBOOT_CMD_HALT 0xcdef0123
#define LINUX_REBOOT_CMD_POWER_OFF 0x4321fedc
#define LINUX_REBOOT_CMD_CAD_ON 0x89abcdef
#define LINUX_REBOOT_CMD_CAD_OFF 0x00000000

long sys_reboot(u64 m1, u64 m2, u64 cmd, u64 arg);
long sys_reboot(u64 m1, u64 m2, u64 cmd, u64 arg) {
  if (!capable_root()) return -EPERM;
  if ((u32)m1 != LINUX_REBOOT_MAGIC1) return -EINVAL;
  switch ((u32)cmd) {
    case LINUX_REBOOT_CMD_RESTART:
      pr_notice("reboot: restarting system\n");
      vfs_sync_all();
      machine_restart();
    case LINUX_REBOOT_CMD_HALT:
      pr_notice("reboot: system halted\n");
      vfs_sync_all();
      machine_halt();
    case LINUX_REBOOT_CMD_POWER_OFF:
      pr_notice("reboot: power down\n");
      vfs_sync_all();
      pr_notice("reboot: filesystems synced\n");
      machine_poweroff();
    case LINUX_REBOOT_CMD_CAD_ON:
    case LINUX_REBOOT_CMD_CAD_OFF:
      return 0;
    default:
      return -EINVAL;
  }
}

long sys_syslog(u64 type, u64 buf, u64 len);
long sys_syslog(u64 type, u64 buf, u64 len) {
  switch (type) {
    case 2: /* READ */
    case 3: /* READ_ALL */
    case 4: { /* READ_CLEAR */
      static size_t clear_pos;
      size_t pos = type == 2 ? clear_pos : (klog_size() > 65536 ? klog_size() - 65536 : 0);
      if (type != 2 && pos < clear_pos) pos = clear_pos;
      char *k = kmalloc(MIN(len, (u64)65536), 0);
      if (!k) return -ENOMEM;
      size_t n = klog_read(k, MIN(len, (u64)65536), &pos);
      long r = copy_to_user(buf, k, n) ? -EFAULT : (long)n;
      kfree(k);
      if (type == 4) clear_pos = klog_size();
      return r;
    }
    case 5: /* CLEAR */
      return 0;
    case 8: /* CONSOLE_LEVEL */
      console_loglevel = CLAMP((int)len, 1, 8);
      return 0;
    case 9: /* SIZE_UNREAD */
    case 10: /* SIZE_BUFFER */
      return 65536;
    default:
      return 0;
  }
}
