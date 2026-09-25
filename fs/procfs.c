/*
 * procfs: system and per-process information. File contents are generated
 * when the file is opened (snapshot semantics) and served from a buffer.
 */
#include <olux/fs.h>
#include <olux/irq.h>
#include <olux/kernel.h>
#include <olux/mm.h>
#include <olux/process.h>
#include <olux/sched.h>
#include <olux/smp.h>
#include <olux/tty.h>
#include <olux/vm.h>

enum pent {
  P_ROOT = 1,
  P_SELF,
  P_MEMINFO,
  P_CPUINFO,
  P_UPTIME,
  P_LOADAVG,
  P_STAT,
  P_VERSION,
  P_MOUNTS,
  P_CMDLINE,
  P_FILESYSTEMS,
  P_INTERRUPTS,
  P_KMSG,
  P_DEVICES,
  P_PID_DIR,
  P_PID_STAT,
  P_PID_STATUS,
  P_PID_CMDLINE,
  P_PID_COMM,
  P_PID_EXE,
  P_PID_CWD,
  P_PID_ROOT,
  P_PID_FDDIR,
  P_PID_FD,
  P_PID_ENVIRON,
  P_PID_MAPS,
  P_PID_STATM,
  P_PID_MOUNTS,
  P_THREAD_SELF,
  P_NET,
  P_NET_FILE,
};

struct pnode {
  enum pent type;
  int pid;
  int fd;
};

static const struct {
  const char *name;
  enum pent type;
  mode_t mode;
} root_entries[] = {
    {"self", P_SELF, S_IFLNK | 0777},
    {"thread-self", P_THREAD_SELF, S_IFLNK | 0777},
    {"meminfo", P_MEMINFO, S_IFREG | 0444},
    {"cpuinfo", P_CPUINFO, S_IFREG | 0444},
    {"uptime", P_UPTIME, S_IFREG | 0444},
    {"loadavg", P_LOADAVG, S_IFREG | 0444},
    {"stat", P_STAT, S_IFREG | 0444},
    {"version", P_VERSION, S_IFREG | 0444},
    {"mounts", P_MOUNTS, S_IFREG | 0444},
    {"cmdline", P_CMDLINE, S_IFREG | 0444},
    {"filesystems", P_FILESYSTEMS, S_IFREG | 0444},
    {"interrupts", P_INTERRUPTS, S_IFREG | 0444},
    {"kmsg", P_KMSG, S_IFREG | 0400},
    {"devices", P_DEVICES, S_IFREG | 0444},
    {"net", P_NET, S_IFDIR | 0555},
};

static const struct {
  const char *name;
  enum pent type;
  mode_t mode;
} pid_entries[] = {
    {"stat", P_PID_STAT, S_IFREG | 0444},       {"status", P_PID_STATUS, S_IFREG | 0444},
    {"cmdline", P_PID_CMDLINE, S_IFREG | 0444}, {"comm", P_PID_COMM, S_IFREG | 0444},
    {"exe", P_PID_EXE, S_IFLNK | 0777},         {"cwd", P_PID_CWD, S_IFLNK | 0777},
    {"root", P_PID_ROOT, S_IFLNK | 0777},       {"fd", P_PID_FDDIR, S_IFDIR | 0500},
    {"environ", P_PID_ENVIRON, S_IFREG | 0400}, {"maps", P_PID_MAPS, S_IFREG | 0444},
    {"statm", P_PID_STATM, S_IFREG | 0444},     {"mounts", P_PID_MOUNTS, S_IFREG | 0444},
};

/* /proc/net/<name> files registered by the network stack */
#define MAX_NET_FILES 16
static struct {
  const char *name;
  void (*show)(seq_printf_t pr, void *ctx);
} net_files[MAX_NET_FILES];
static int nnet_files;

void proc_net_register(const char *name, void (*show)(seq_printf_t pr, void *ctx)) {
  if (nnet_files < MAX_NET_FILES) net_files[nnet_files++] = (typeof(net_files[0])){name, show};
}

static const struct inode_operations proc_dir_iops, proc_link_iops;
static const struct file_operations proc_dir_fops, proc_file_fops;
extern u64 load_avg[3]; /* fixed point, <<16 */

static struct inode *proc_inode(struct super_block *sb, enum pent type, int pid, int fd, mode_t mode) {
  struct inode *i = new_inode(sb, mode);
  if (!i) return NULL;
  struct pnode *n = kzalloc(sizeof(*n), 0);
  if (!n) {
    inode_put(i);
    return NULL;
  }
  n->type = type;
  n->pid = pid;
  n->fd = fd;
  i->priv = n;
  i->ino = ((u64)pid << 20) | ((u64)type << 12) | (u64)(fd & 0xfff);
  if (S_ISDIR(mode)) {
    i->i_op = &proc_dir_iops;
    i->f_op = &proc_dir_fops;
    i->nlink = 2;
  } else if (S_ISLNK(mode)) {
    i->i_op = &proc_link_iops;
  } else {
    i->f_op = &proc_file_fops;
  }
  struct process *p = pid ? process_find(pid) : NULL;
  if (p) {
    i->uid = p->cred.uid;
    i->gid = p->cred.gid;
  }
  return i;
}

static void proc_evict(struct inode *i) { kfree(i->priv); }

static struct pnode *pn(struct inode *i) { return i->priv; }

static int proc_lookup(struct inode *dir, const char *name, struct inode **out) {
  struct pnode *d = pn(dir);
  if (d->type == P_ROOT) {
    for (unsigned k = 0; k < ARRAY_SIZE(root_entries); k++)
      if (!strcmp(name, root_entries[k].name)) {
        *out = proc_inode(dir->sb, root_entries[k].type, 0, 0, root_entries[k].mode);
        return *out ? 0 : -ENOMEM;
      }
    char *end;
    long pid = strtol(name, &end, 10);
    if (*end || pid <= 0) return -ENOENT;
    struct process *p = process_find((int)pid);
    if (!p) {
      /* a thread id also resolves (like /proc/<tid>) */
      struct thread *t = thread_find((int)pid);
      if (!t || !t->proc) return -ENOENT;
    }
    *out = proc_inode(dir->sb, P_PID_DIR, (int)pid, 0, S_IFDIR | 0555);
    return *out ? 0 : -ENOMEM;
  }
  if (d->type == P_PID_DIR) {
    for (unsigned k = 0; k < ARRAY_SIZE(pid_entries); k++)
      if (!strcmp(name, pid_entries[k].name)) {
        *out = proc_inode(dir->sb, pid_entries[k].type, d->pid, 0, pid_entries[k].mode);
        return *out ? 0 : -ENOMEM;
      }
    return -ENOENT;
  }
  if (d->type == P_NET) {
    for (int k = 0; k < nnet_files; k++)
      if (!strcmp(name, net_files[k].name)) {
        *out = proc_inode(dir->sb, P_NET_FILE, 0, k, S_IFREG | 0444);
        return *out ? 0 : -ENOMEM;
      }
    return -ENOENT;
  }
  if (d->type == P_PID_FDDIR) {
    char *end;
    long fd = strtol(name, &end, 10);
    struct process *p = process_find(d->pid);
    if (*end || fd < 0 || !p || !p->files || fd >= p->files->max || !p->files->fd[fd]) return -ENOENT;
    *out = proc_inode(dir->sb, P_PID_FD, d->pid, (int)fd, S_IFLNK | 0700);
    return *out ? 0 : -ENOMEM;
  }
  return -ENOENT;
}

static int proc_iterate(struct file *f, struct dir_context *ctx) {
  struct pnode *d = pn(f->inode);
  char name[24];
  loff_t idx = 0;
#define EMIT(nm, ino, type)                                      \
  do {                                                           \
    if (idx++ >= ctx->pos) {                                     \
      if (!ctx->actor(ctx, nm, strlen(nm), ino, type)) return 0; \
      ctx->pos = idx;                                            \
    }                                                            \
  } while (0)
  EMIT(".", f->inode->ino, DT_DIR);
  EMIT("..", 1, DT_DIR);
  if (d->type == P_ROOT) {
    for (unsigned k = 0; k < ARRAY_SIZE(root_entries); k++)
      EMIT(root_entries[k].name, 100 + k, mode_to_dtype(root_entries[k].mode));
    struct process *p;
    list_for_each_entry(p, &all_processes, all_link) {
      if (p->state == PROC_ZOMBIE && 0) continue;
      snprintf(name, sizeof(name), "%d", p->pid);
      EMIT(name, (u64)p->pid << 20, DT_DIR);
    }
  } else if (d->type == P_PID_DIR) {
    for (unsigned k = 0; k < ARRAY_SIZE(pid_entries); k++)
      EMIT(pid_entries[k].name, ((u64)d->pid << 20) | (pid_entries[k].type << 12), mode_to_dtype(pid_entries[k].mode));
  } else if (d->type == P_NET) {
    for (int k = 0; k < nnet_files; k++) EMIT(net_files[k].name, 0x7000000 + k, DT_REG);
  } else if (d->type == P_PID_FDDIR) {
    struct process *p = process_find(d->pid);
    if (p && p->files)
      for (int fd = 0; fd < p->files->max; fd++)
        if (p->files->fd[fd]) {
          snprintf(name, sizeof(name), "%d", fd);
          EMIT(name, ((u64)d->pid << 20) | (P_PID_FD << 12) | fd, DT_LNK);
        }
  }
#undef EMIT
  return 0;
}

static ssize_t path_of_file(struct file *f, char *buf, size_t size) {
  if (f->path.dentry) {
    int n = d_path(&f->path, buf, size);
    return n < 0 ? n : n;
  }
  const char *kind = S_ISFIFO(f->inode->mode) ? "pipe" : S_ISSOCK(f->inode->mode) ? "socket" : "anon_inode";
  return snprintf(buf, size, "%s:[%llu]", kind, (unsigned long long)f->inode->ino);
}

static ssize_t proc_readlink(struct inode *i, char *buf, size_t size) {
  struct pnode *n = pn(i);
  char tmp[PATH_MAX > 512 ? 512 : PATH_MAX];
  ssize_t len;
  struct process *p = n->pid ? process_find(n->pid) : NULL;
  switch (n->type) {
    case P_SELF:
      len = snprintf(tmp, sizeof(tmp), "%d", current->proc->pid);
      break;
    case P_THREAD_SELF:
      len = snprintf(tmp, sizeof(tmp), "%d/task/%d", current->proc->pid, current->tid);
      break;
    case P_PID_EXE:
      if (!p) return -ENOENT;
      len = snprintf(tmp, sizeof(tmp), "%s", p->exe);
      break;
    case P_PID_CWD:
    case P_PID_ROOT:
      if (!p || !p->cwd.dentry) return -ENOENT;
      len = d_path(n->type == P_PID_CWD ? &p->cwd : &p->root, tmp, sizeof(tmp));
      break;
    case P_PID_FD: {
      if (!p || !p->files || n->fd >= p->files->max || !p->files->fd[n->fd]) return -ENOENT;
      len = path_of_file(p->files->fd[n->fd], tmp, sizeof(tmp));
      break;
    }
    default:
      return -EINVAL;
  }
  if (len < 0) return len;
  len = MIN((size_t)len, size);
  memcpy(buf, tmp, len);
  return len;
}

/* ---- generated file contents ---- */

struct pbuf {
  char *data;
  size_t len, cap;
};

static void vpb(struct pbuf *b, const char *fmt, va_list ap0) {
  va_list ap;
  for (;;) {
    va_copy(ap, ap0);
    size_t room = b->cap - b->len;
    int n = vsnprintf(b->data + b->len, room, fmt, ap);
    va_end(ap);
    if ((size_t)n < room) {
      b->len += n;
      return;
    }
    size_t ncap = MAX(b->cap * 2, b->len + n + 1);
    char *nd = kmalloc(ncap, 0);
    if (!nd) return;
    memcpy(nd, b->data, b->len);
    kfree(b->data);
    b->data = nd;
    b->cap = ncap;
  }
}

__printf(2, 3) static void pb(struct pbuf *b, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vpb(b, fmt, ap);
  va_end(ap);
}

__printf(2, 3) static void pb_ctx(void *ctx, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vpb(ctx, fmt, ap);
  va_end(ap);
}

static char state_char(struct process *p) {
  if (p->state == PROC_ZOMBIE) return 'Z';
  if (p->group_stop) return 'T';
  struct thread *t;
  char s = 'S';
  list_for_each_entry(t, &p->threads, thread_link) {
    if (t->state == TASK_RUNNING) return 'R';
    if (t->state == TASK_UNINTERRUPTIBLE) s = 'D';
  }
  return s;
}

static u64 ticks100(u64 ns) { return ns / (NSEC_PER_SEC / 100); }

static void gen_pid_stat(struct pbuf *b, struct process *p) {
  u64 ut = p->utime, st = p->stime;
  struct thread *t, *main = NULL;
  list_for_each_entry(t, &p->threads, thread_link) {
    ut += t->utime;
    st += t->stime;
    if (!main) main = t;
  }
  u64 vsize = 0, rss = 0;
  if (p->mm) {
    struct vma *v;
    list_for_each_entry(v, &p->mm->vmas, link) vsize += v->end - v->start;
    rss = p->mm->rss_pages;
  }
  int tty_nr = p->tty ? (int)MKDEV(204, 64 + p->tty->index) : 0;
  pb(b, "%d (%s) %c %d %d %d %d %d 0 %llu 0 0 0 %llu %llu %llu %llu %d %d %d 0 %llu %llu %llu\n", p->pid, p->comm,
     state_char(p), p->parent ? p->parent->pid : 0, p->pgid, p->sid, tty_nr, p->tty ? p->tty->pgrp : -1,
     (unsigned long long)p->min_flt, (unsigned long long)ticks100(ut), (unsigned long long)ticks100(st),
     (unsigned long long)ticks100(p->cutime), (unsigned long long)ticks100(p->cstime),
     main ? 20 - (main->prio - PRIO_NORMAL) : 20, main ? main->nice : 0, p->nr_threads,
     (unsigned long long)ticks100(p->start_time), (unsigned long long)vsize, (unsigned long long)rss);
}

static void gen_pid_status(struct pbuf *b, struct process *p) {
  static const char *const names[] = {"R (running)", "S (sleeping)", "D (disk sleep)", "T (stopped)", "Z (zombie)"};
  char s = state_char(p);
  const char *sn = s == 'R' ? names[0] : s == 'S' ? names[1] : s == 'D' ? names[2] : s == 'T' ? names[3] : names[4];
  u64 vsize = 0;
  if (p->mm) {
    struct vma *v;
    list_for_each_entry(v, &p->mm->vmas, link) vsize += v->end - v->start;
  }
  struct thread *t = list_empty(&p->threads) ? NULL : list_first_entry(&p->threads, struct thread, thread_link);
  pb(b,
     "Name:\t%s\nUmask:\t%04o\nState:\t%s\nTgid:\t%d\nPid:\t%d\nPPid:\t%d\nUid:\t%u\t%u\t%u\t%u\nGid:\t%u\t%u\t%u\t%u\n"
     "VmSize:\t%llu kB\nVmRSS:\t%llu kB\nThreads:\t%d\nSigPnd:\t%016llx\nSigBlk:\t%016llx\n",
     p->comm, p->umask, sn, p->pid, p->pid, p->parent ? p->parent->pid : 0, p->cred.uid, p->cred.euid, p->cred.suid,
     p->cred.euid, p->cred.gid, p->cred.egid, p->cred.sgid, p->cred.egid, (unsigned long long)(vsize >> 10),
     (unsigned long long)((p->mm ? p->mm->rss_pages : 0) * 4), p->nr_threads, (unsigned long long)p->shared_pending,
     (unsigned long long)(t ? t->sig_blocked : 0));
}

static void gen_user_range(struct pbuf *b, struct process *p, u64 start, u64 end) {
  if (!p->mm || end <= start) return;
  for (u64 a = start; a < end;) {
    u8 *k = mm_user_page(p->mm, a, false);
    size_t n = MIN(end - a, PAGE_SIZE - (a & ~PAGE_MASK));
    if (!k) break;
    for (size_t i = 0; i < n; i++) pb(b, "%c", k[i]);
    a += n;
  }
}

static void gen_maps(struct pbuf *b, struct process *p) {
  if (!p->mm) return;
  struct vma *v;
  list_for_each_entry(v, &p->mm->vmas, link) {
    char path[128] = "";
    if (v->file && v->file->path.dentry)
      d_path(&v->file->path, path, sizeof(path));
    else if (v->flags & VM_GROWSDOWN)
      strlcpy(path, "[stack]", sizeof(path));
    else if (v->start == p->mm->sigtramp)
      strlcpy(path, "[sigpage]", sizeof(path));
    else if (v->start >= p->mm->brk_start && v->end <= ALIGN_UP(p->mm->brk, PAGE_SIZE) + PAGE_SIZE &&
             v->start < p->mm->brk + PAGE_SIZE)
      strlcpy(path, "[heap]", sizeof(path));
    pb(b, "%010llx-%010llx %c%c%c%c %08llx 00:00 0 %s\n", (unsigned long long)v->start, (unsigned long long)v->end,
       (v->flags & VM_READ) ? 'r' : '-', (v->flags & VM_WRITE) ? 'w' : '-', (v->flags & VM_EXEC) ? 'x' : '-',
       (v->flags & VM_SHARED) ? 's' : 'p', (unsigned long long)v->file_off, path);
  }
}

static void gen_cpuinfo(struct pbuf *b) {
  u64 midr = read_sysreg(midr_el1);
  for (int i = 0; i < nr_cpus_possible; i++) {
    if (!cpus[i].online) continue;
    pb(b,
       "processor\t: %d\nBogoMIPS\t: %llu.00\nFeatures\t: fp asimd crc32\nCPU implementer\t: 0x%02llx\n"
       "CPU architecture: 8\nCPU variant\t: 0x%llx\nCPU part\t: 0x%03llx\nCPU revision\t: %llu\n\n",
       i, (unsigned long long)(read_sysreg(cntfrq_el0) / 500000), (unsigned long long)(midr >> 24),
       (unsigned long long)((midr >> 20) & 0xf), (unsigned long long)((midr >> 4) & 0xfff),
       (unsigned long long)(midr & 0xf));
  }
}

static void gen_stat(struct pbuf *b) {
  u64 tu = 0, ts = 0, ti = 0;
  for (int i = 0; i < nr_cpus_possible; i++) ti += cpus[i].idle_ticks;
  struct thread *t;
  list_for_each_entry(t, &all_threads, all_link) {
    tu += t->utime;
    ts += t->stime;
  }
  const u64 tickns = TICK_NSEC;
  pb(b, "cpu  %llu 0 %llu %llu 0 0 0 0 0 0\n", (unsigned long long)ticks100(tu), (unsigned long long)ticks100(ts),
     (unsigned long long)ticks100(ti * tickns));
  for (int i = 0; i < nr_cpus_possible; i++)
    if (cpus[i].online)
      pb(b, "cpu%d %llu 0 %llu %llu 0 0 0 0 0 0\n", i,
         (unsigned long long)ticks100((cpus[i].ticks - cpus[i].idle_ticks) * tickns / 2),
         (unsigned long long)ticks100((cpus[i].ticks - cpus[i].idle_ticks) * tickns / 2),
         (unsigned long long)ticks100(cpus[i].idle_ticks * tickns));
  u64 ctxt = 0, intr = 0;
  for (int i = 0; i < nr_cpus_possible; i++) {
    ctxt += cpus[i].ctx_switches;
    intr += cpus[i].irq_count;
  }
  int nproc = 0, running = 0;
  struct process *p;
  list_for_each_entry(p, &all_processes, all_link) {
    nproc++;
    if (state_char(p) == 'R') running++;
  }
  pb(b, "intr %llu\nctxt %llu\nbtime %llu\nprocesses %d\nprocs_running %d\nprocs_blocked 0\n", (unsigned long long)intr,
     (unsigned long long)ctxt, (unsigned long long)((ktime_realtime_ns() - ktime_ns()) / NSEC_PER_SEC), nproc, running);
}

static int generate(struct inode *i, struct pbuf *b) {
  struct pnode *n = pn(i);
  struct process *p = n->pid ? process_find(n->pid) : NULL;
  if (n->pid && !p) {
    struct thread *t = thread_find(n->pid);
    p = t ? t->proc : NULL;
    if (!p) return -ESRCH;
  }
  switch (n->type) {
    case P_MEMINFO: {
      u64 total = nr_total_pages() * 4, free = nr_free_pages() * 4;
      pb(b,
         "MemTotal:       %8llu kB\nMemFree:        %8llu kB\nMemAvailable:   %8llu kB\nBuffers:        %8llu kB\n"
         "Cached:         %8llu kB\nSwapCached:            0 kB\nSwapTotal:             0 kB\nSwapFree:              0 "
         "kB\n"
         "Shmem:                 0 kB\nSlab:           %8llu kB\n",
         (unsigned long long)total, (unsigned long long)free, (unsigned long long)free, 0ULL, 0ULL,
         (unsigned long long)(kmalloc_bytes_in_use() >> 10));
      break;
    }
    case P_CPUINFO:
      gen_cpuinfo(b);
      break;
    case P_UPTIME: {
      u64 up = ktime_ns(), idle = 0;
      for (int c = 0; c < nr_cpus_possible; c++) idle += cpus[c].idle_ticks * TICK_NSEC;
      pb(b, "%llu.%02llu %llu.%02llu\n", (unsigned long long)(up / NSEC_PER_SEC),
         (unsigned long long)(up / 10000000 % 100), (unsigned long long)(idle / NSEC_PER_SEC),
         (unsigned long long)(idle / 10000000 % 100));
      break;
    }
    case P_LOADAVG: {
      int nthr = 0;
      struct thread *t;
      list_for_each_entry(t, &all_threads, all_link) nthr++;
      pb(b, "%llu.%02llu %llu.%02llu %llu.%02llu %llu/%d %d\n", (unsigned long long)(load_avg[0] >> 16),
         (unsigned long long)((load_avg[0] & 0xffff) * 100 >> 16), (unsigned long long)(load_avg[1] >> 16),
         (unsigned long long)((load_avg[1] & 0xffff) * 100 >> 16), (unsigned long long)(load_avg[2] >> 16),
         (unsigned long long)((load_avg[2] & 0xffff) * 100 >> 16), (unsigned long long)nr_running_total(), nthr,
         current->tid);
      break;
    }
    case P_STAT:
      gen_stat(b);
      break;
    case P_VERSION:
      pb(b, "OluxOS version %s (%s) AArch64\n", OLUX_VERSION, OLUX_GITREV);
      break;
    case P_MOUNTS:
    case P_PID_MOUNTS: {
      char *tmp = kmalloc(4096, 0);
      if (tmp) {
        int len = mounts_show(tmp, 4096);
        pb(b, "%.*s", len, tmp);
        kfree(tmp);
      }
      break;
    }
    case P_CMDLINE:
      pb(b, "%s\n", kernel_cmdline());
      break;
    case P_FILESYSTEMS:
      pb(b, "nodev\ttmpfs\nnodev\tramfs\nnodev\tdevtmpfs\nnodev\tproc\n\tvfat\n\text4\n");
      break;
    case P_INTERRUPTS:
      for (int irq = 0; irq < NR_IRQS; irq++)
        if (irq_name(irq)) pb(b, "%4d: %10llu  %s\n", irq, (unsigned long long)irq_count(irq), irq_name(irq));
      break;
    case P_NET_FILE:
      if (n->fd < nnet_files) net_files[n->fd].show(pb_ctx, b);
      break;
    case P_DEVICES:
      pb(b,
         "Character devices:\n  1 mem\n  4 tty\n  5 /dev/tty\n  5 /dev/console\n 29 fb\n204 ttyAMA\n\nBlock devices:\n"
         "179 mmc\n254 virtblk\n");
      break;
    case P_KMSG: {
      size_t pos = 0;
      char tmp[256];
      size_t k;
      while ((k = klog_read(tmp, sizeof(tmp), &pos))) pb(b, "%.*s", (int)k, tmp);
      break;
    }
    case P_PID_STAT:
      gen_pid_stat(b, p);
      break;
    case P_PID_STATUS:
      gen_pid_status(b, p);
      break;
    case P_PID_STATM: {
      u64 vsize = 0;
      if (p->mm) {
        struct vma *v;
        list_for_each_entry(v, &p->mm->vmas, link) vsize += v->end - v->start;
      }
      pb(b, "%llu %llu 0 0 0 0 0\n", (unsigned long long)(vsize / PAGE_SIZE),
         (unsigned long long)(p->mm ? p->mm->rss_pages : 0));
      break;
    }
    case P_PID_CMDLINE:
      if (p->mm) gen_user_range(b, p, p->mm->arg_start, p->mm->arg_end);
      break;
    case P_PID_ENVIRON:
      if (p->mm) gen_user_range(b, p, p->mm->env_start, p->mm->env_end);
      break;
    case P_PID_COMM:
      pb(b, "%s\n", p->comm);
      break;
    case P_PID_MAPS:
      gen_maps(b, p);
      break;
    default:
      return -EINVAL;
  }
  return 0;
}

static int proc_open(struct inode *i, struct file *f) {
  struct pbuf *b = kzalloc(sizeof(*b), 0);
  if (!b) return -ENOMEM;
  b->cap = 1024;
  b->data = kmalloc(b->cap, 0);
  if (!b->data) {
    kfree(b);
    return -ENOMEM;
  }
  int r = generate(i, b);
  if (r) {
    kfree(b->data);
    kfree(b);
    return r;
  }
  f->priv = b;
  return 0;
}

static int proc_release(struct inode *i, struct file *f) {
  struct pbuf *b = f->priv;
  if (b) {
    kfree(b->data);
    kfree(b);
  }
  return 0;
}

static ssize_t proc_read(struct file *f, struct iobuf *buf, loff_t *pos) {
  struct pbuf *b = f->priv;
  if (*pos >= (loff_t)b->len) return 0;
  size_t n = MIN(buf->len, b->len - *pos);
  if (iob_write(buf, 0, b->data + *pos, n)) return -EFAULT;
  *pos += n;
  return n;
}

static loff_t proc_llseek(struct file *f, loff_t off, int whence) {
  struct pbuf *b = f->priv;
  loff_t base = whence == SEEK_SET ? 0 : whence == SEEK_CUR ? f->pos : (loff_t)(b ? b->len : 0);
  if (base + off < 0) return -EINVAL;
  return f->pos = base + off;
}

static const struct inode_operations proc_dir_iops = {.lookup = proc_lookup};
static const struct inode_operations proc_link_iops = {.readlink = proc_readlink};
static const struct file_operations proc_dir_fops = {.iterate = proc_iterate};
static const struct file_operations proc_file_fops = {
    .open = proc_open, .release = proc_release, .read = proc_read, .llseek = proc_llseek};
static const struct super_operations proc_sops = {.evict = proc_evict};

static int proc_mount(struct super_block *sb, const char *dev, const char *data) {
  sb->s_op = &proc_sops;
  struct inode *root = proc_inode(sb, P_ROOT, 0, 0, S_IFDIR | 0555);
  if (!root) return -ENOMEM;
  root->ino = 1;
  sb->root = d_alloc(NULL, "/", root);
  return sb->root ? 0 : -ENOMEM;
}

static struct fs_type proc_type = {.name = "proc", .mount = proc_mount, .nodev = true};

void procfs_register(void);
void procfs_register(void) { register_filesystem(&proc_type); }
