/*
 * execve: ELF64 AArch64 loader (ET_EXEC, static-PIE / ET_DYN, PT_INTERP),
 * "#!" scripts, argument/environment/auxv setup. Segments are mapped as
 * demand-paged file-backed private mappings.
 */
#include <olux/elf.h>
#include <olux/fs.h>
#include <olux/kernel.h>
#include <olux/mm.h>
#include <olux/process.h>
#include <olux/random.h>
#include <olux/sched.h>
#include <olux/uaccess.h>
#include <olux/vm.h>

#define ARG_MAX (256 * 1024)
#define MAX_ARGS 8192

struct exec_args {
  char *buf; /* argv strings then envp strings, NUL separated */
  size_t used;
  int argc, envc;
};

static int push_str(struct exec_args *a, const char *s, size_t n) {
  if (a->used + n + 1 > ARG_MAX) return -E2BIG;
  memcpy(a->buf + a->used, s, n);
  a->buf[a->used + n] = '\0';
  a->used += n + 1;
  return 0;
}

static int copy_strings(struct exec_args *a, char *const *vec, bool kernel, int *count) {
  if (!vec) return 0;
  for (int i = 0; i < MAX_ARGS; i++) {
    u64 p;
    if (kernel) p = (u64)vec[i];
    else if (get_user(p, (u64)&vec[i])) return -EFAULT;
    if (!p) return 0;
    if (kernel) {
      int r = push_str(a, (const char *)p, strlen((const char *)p));
      if (r) return r;
    } else {
      long room = ARG_MAX - a->used;
      if (room <= 1) return -E2BIG;
      long n = strncpy_from_user(a->buf + a->used, p, room);
      if (n == -ENAMETOOLONG) return -E2BIG;
      if (n < 0) return (int)n;
      a->used += n + 1;
    }
    (*count)++;
  }
  return -E2BIG;
}

/* Insert strings at the front of argv (for #! interpreters). */
static int prepend_args(struct exec_args *a, const char *const *strs, int n) {
  size_t need = 0;
  for (int i = 0; i < n; i++) need += strlen(strs[i]) + 1;
  if (a->used + need > ARG_MAX) return -E2BIG;
  memmove(a->buf + need, a->buf, a->used);
  size_t off = 0;
  for (int i = 0; i < n; i++) {
    size_t l = strlen(strs[i]) + 1;
    memcpy(a->buf + off, strs[i], l);
    off += l;
  }
  a->used += need;
  a->argc += n;
  return 0;
}

/* Drop argv[0] (replaced by the script path for #!). */
static void drop_first_arg(struct exec_args *a) {
  if (!a->argc) return;
  size_t l = strlen(a->buf) + 1;
  memmove(a->buf, a->buf + l, a->used - l);
  a->used -= l;
  a->argc--;
}

struct loaded_elf {
  u64 entry, load_bias, phdr, phnum, phent, end, start;
};

static int check_ehdr(const Elf64_Ehdr *e) {
  if (memcmp(e->e_ident, ELFMAG, 4) || e->e_ident[EI_CLASS] != ELFCLASS64 ||
      e->e_ident[EI_DATA] != ELFDATA2LSB || e->e_machine != EM_AARCH64)
    return -ENOEXEC;
  if (e->e_type != ET_EXEC && e->e_type != ET_DYN) return -ENOEXEC;
  if (e->e_phentsize != sizeof(Elf64_Phdr) || e->e_phnum == 0 || e->e_phnum > 64) return -ENOEXEC;
  return 0;
}

static int load_elf(struct mm *mm, struct file *f, const Elf64_Ehdr *eh, Elf64_Phdr *ph, u64 base_hint,
                    struct loaded_elf *out) {
  u64 lo = ~0UL, hi = 0;
  for (int i = 0; i < eh->e_phnum; i++) {
    if (ph[i].p_type != PT_LOAD) continue;
    if (ph[i].p_filesz > ph[i].p_memsz) return -ENOEXEC;
    if ((ph[i].p_vaddr - ph[i].p_offset) & (PAGE_SIZE - 1)) return -ENOEXEC;
    lo = MIN(lo, ALIGN_DOWN(ph[i].p_vaddr, PAGE_SIZE));
    hi = MAX(hi, ph[i].p_vaddr + ph[i].p_memsz);
  }
  if (hi <= lo) return -ENOEXEC;
  u64 bias = 0;
  if (eh->e_type == ET_DYN) bias = base_hint - lo;
  if (lo + bias < USER_MIN_ADDR || hi + bias > USER_VA_END || hi + bias < lo + bias) return -ENOEXEC;
  out->phdr = 0;
  for (int i = 0; i < eh->e_phnum; i++) {
    Elf64_Phdr *p = &ph[i];
    if (p->p_type == PT_PHDR) out->phdr = p->p_vaddr + bias;
    if (p->p_type != PT_LOAD || p->p_memsz == 0) continue;
    u64 start = ALIGN_DOWN(p->p_vaddr + bias, PAGE_SIZE);
    u64 end = ALIGN_UP(p->p_vaddr + bias + p->p_memsz, PAGE_SIZE);
    u64 lead = (p->p_vaddr + bias) - start;
    if (p->p_offset < lead) return -ENOEXEC;
    u32 flags = 0;
    if (p->p_flags & PF_R) flags |= VM_READ;
    if (p->p_flags & PF_W) flags |= VM_WRITE | VM_READ;
    if (p->p_flags & PF_X) flags |= VM_EXEC | VM_READ;
    if (vma_find_intersect(mm, start, end)) return -ENOEXEC;
    int r = map_vma(mm, start, end, flags, f, p->p_offset - lead, lead + p->p_filesz);
    if (r) return r;
    if (!out->phdr && p->p_offset <= eh->e_phoff &&
        eh->e_phoff + eh->e_phnum * sizeof(Elf64_Phdr) <= p->p_offset + p->p_filesz)
      out->phdr = p->p_vaddr + bias + (eh->e_phoff - p->p_offset);
  }
  out->entry = eh->e_entry + bias;
  out->load_bias = bias;
  out->phnum = eh->e_phnum;
  out->phent = sizeof(Elf64_Phdr);
  out->start = lo + bias;
  out->end = hi + bias;
  return 0;
}

static int read_ehdr(struct file *f, Elf64_Ehdr *eh, Elf64_Phdr **ph) {
  if (kernel_pread(f, eh, sizeof(*eh), 0) != sizeof(*eh)) return -ENOEXEC;
  int r = check_ehdr(eh);
  if (r) return r;
  size_t sz = eh->e_phnum * sizeof(Elf64_Phdr);
  *ph = kmalloc(sz, 0);
  if (!*ph) return -ENOMEM;
  if (kernel_pread(f, *ph, sz, eh->e_phoff) != (ssize_t)sz) {
    kfree(*ph);
    return -ENOEXEC;
  }
  return 0;
}

/* Kill all other threads of the current process (exec in a threaded program). */
static void de_thread(void) {
  struct process *p = current->proc;
  if (p->nr_threads == 1) return;
  p->exiting = true;
  struct thread *t;
  list_for_each_entry(t, &p->threads, thread_link)
    if (t != current) send_signal_thread(t, SIGKILL, NULL);
  wait_event(p->child_wait, p->nr_threads == 1);
  p->exiting = false;
  /* discard the SIGKILL queued for ourselves by other exiting threads */
  current->sig_pending &= ~sigmask(SIGKILL);
}

static const u32 sigtramp_code[] = {
    0xd2801168, /* mov x8, #139 (rt_sigreturn) */
    0xd4000001, /* svc #0 */
};

static int map_sigtramp(struct mm *mm) {
  u64 addr = mm_get_unmapped_area(mm, PAGE_SIZE);
  if (!addr) return -ENOMEM;
  int r = map_vma(mm, addr, addr + PAGE_SIZE, VM_READ | VM_EXEC, NULL, 0, 0);
  if (r) return r;
  u32 *page = mm_user_page(mm, addr, false);
  if (!page) return -ENOMEM;
  /* the page was just zero-faulted and is private to this mm */
  memcpy(page, sigtramp_code, sizeof(sigtramp_code));
  sync_icache_range(page, sizeof(sigtramp_code));
  mm->sigtramp = addr;
  return 0;
}

static u64 hwcap(void) {
  u64 isar0 = read_sysreg(id_aa64isar0_el1), pfr0 = read_sysreg(id_aa64pfr0_el1);
  u64 cap = 0;
  if (((pfr0 >> 16) & 0xf) != 0xf) cap |= 1 << 0; /* FP */
  if (((pfr0 >> 20) & 0xf) != 0xf) cap |= 1 << 1; /* ASIMD */
  cap |= 1 << 11;                                   /* CPUID via MRS is not emulated: leave off */
  cap &= ~(1UL << 11);
  if ((isar0 >> 4) & 0xf) cap |= 1 << 3;            /* AES */
  if (((isar0 >> 4) & 0xf) >= 2) cap |= 1 << 4;     /* PMULL */
  if ((isar0 >> 8) & 0xf) cap |= 1 << 5;            /* SHA1 */
  if ((isar0 >> 12) & 0xf) cap |= 1 << 6;           /* SHA2 */
  if ((isar0 >> 16) & 0xf) cap |= 1 << 7;           /* CRC32 */
  if ((isar0 >> 20) & 0xf) cap |= 1 << 8;           /* ATOMICS */
  return cap;
}

#define AT_NULL 0
#define AT_PHDR 3
#define AT_PHENT 4
#define AT_PHNUM 5
#define AT_PAGESZ 6
#define AT_BASE 7
#define AT_FLAGS 8
#define AT_ENTRY 9
#define AT_UID 11
#define AT_EUID 12
#define AT_GID 13
#define AT_EGID 14
#define AT_PLATFORM 15
#define AT_HWCAP 16
#define AT_CLKTCK 17
#define AT_SECURE 23
#define AT_RANDOM 25
#define AT_HWCAP2 26
#define AT_EXECFN 31
#define AT_MINSIGSTKSZ 51

static int build_stack(struct mm *mm, struct exec_args *a, struct loaded_elf *exe, u64 interp_base,
                       const char *filename, u64 *sp_out) {
  struct process *p = current->proc;
  u64 sp = mm->stack_top;
  /* strings */
  sp -= a->used;
  u64 strings = sp;
  if (copy_to_user(strings, a->buf, a->used)) return -EFAULT;
  size_t fl = strlen(filename) + 1;
  sp -= fl;
  u64 execfn = sp;
  if (copy_to_user(execfn, filename, fl)) return -EFAULT;
  sp -= 8;
  u64 platform = sp;
  if (copy_to_user(platform, "aarch64", 8)) return -EFAULT;
  sp -= 16;
  u64 rnd = sp;
  u8 rbytes[16];
  get_random_bytes(rbytes, sizeof(rbytes));
  if (copy_to_user(rnd, rbytes, 16)) return -EFAULT;
  sp = ALIGN_DOWN(sp, 16);

  u64 auxv[] = {
      AT_PHDR,   exe->phdr,  AT_PHENT,  exe->phent,        AT_PHNUM,  exe->phnum,  AT_PAGESZ, PAGE_SIZE,
      AT_BASE,   interp_base, AT_FLAGS, 0,                  AT_ENTRY,  exe->entry,  AT_UID,    p->cred.uid,
      AT_EUID,   p->cred.euid, AT_GID,  p->cred.gid,        AT_EGID,   p->cred.egid, AT_SECURE, 0,
      AT_RANDOM, rnd,        AT_HWCAP,  hwcap(),            AT_HWCAP2, 0,            AT_CLKTCK, 100,
      AT_PLATFORM, platform, AT_EXECFN, execfn,             AT_MINSIGSTKSZ, 5120,    AT_NULL,   0,
  };
  size_t words = 1 + a->argc + 1 + a->envc + 1 + ARRAY_SIZE(auxv);
  sp -= words * 8;
  sp = ALIGN_DOWN(sp, 16);
  if (mm->stack_top - sp > USER_STACK_MAX / 2) return -E2BIG;
  u64 *v = kmalloc(words * 8, 0);
  if (!v) return -ENOMEM;
  size_t w = 0;
  v[w++] = a->argc;
  u64 s = strings;
  mm->arg_start = strings;
  for (int i = 0; i < a->argc; i++) {
    v[w++] = s;
    s += strlen(a->buf + (s - strings)) + 1;
  }
  v[w++] = 0;
  mm->arg_end = mm->env_start = s;
  for (int i = 0; i < a->envc; i++) {
    v[w++] = s;
    s += strlen(a->buf + (s - strings)) + 1;
  }
  mm->env_end = s;
  v[w++] = 0;
  memcpy(&v[w], auxv, sizeof(auxv));
  int r = copy_to_user(sp, v, words * 8);
  kfree(v);
  *sp_out = sp;
  return r;
}

static int exec_file(const char *path, struct exec_args *a, int depth) {
  int err;
  struct file *f = vfs_open(AT_FDCWD, path, O_RDONLY, 0, &err);
  if (!f) return err;
  struct inode *ino = f->inode;
  if (!S_ISREG(ino->mode)) {
    file_put(f);
    return -EACCES;
  }
  if ((f->path.mnt->sb->flags & SB_NOEXEC) || (err = permission(ino, 1))) {
    file_put(f);
    return err ? err : -EACCES;
  }
  char hdr[256];
  ssize_t n = kernel_pread(f, hdr, sizeof(hdr) - 1, 0);
  if (n < 0) {
    file_put(f);
    return (int)n;
  }
  hdr[n] = '\0';

  /* ---- #! scripts ---- */
  if (n >= 2 && hdr[0] == '#' && hdr[1] == '!') {
    file_put(f);
    if (depth > 4) return -ELOOP;
    char *nl = strchr(hdr, '\n');
    if (!nl) return -ENOEXEC;
    *nl = '\0';
    char *interp = hdr + 2;
    while (*interp == ' ' || *interp == '\t') interp++;
    char *arg = interp;
    while (*arg && *arg != ' ' && *arg != '\t') arg++;
    if (*arg) {
      *arg++ = '\0';
      while (*arg == ' ' || *arg == '\t') arg++;
      char *e = arg + strlen(arg);
      while (e > arg && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r')) *--e = '\0';
    }
    if (!*interp) return -ENOEXEC;
    drop_first_arg(a);
    const char *pre[3];
    int np = 0;
    pre[np++] = interp;
    if (*arg) pre[np++] = arg;
    pre[np++] = path;
    int r = prepend_args(a, pre, np);
    if (r) return r;
    char ipath[256];
    strlcpy(ipath, interp, sizeof(ipath));
    return exec_file(ipath, a, depth + 1);
  }

  /* ---- ELF ---- */
  Elf64_Ehdr eh;
  Elf64_Phdr *ph;
  int r = read_ehdr(f, &eh, &ph);
  if (r) {
    file_put(f);
    return r;
  }
  char interp_path[256] = "";
  for (int i = 0; i < eh.e_phnum; i++) {
    if (ph[i].p_type != PT_INTERP) continue;
    if (ph[i].p_filesz == 0 || ph[i].p_filesz >= sizeof(interp_path) ||
        kernel_pread(f, interp_path, ph[i].p_filesz, ph[i].p_offset) != (ssize_t)ph[i].p_filesz) {
      kfree(ph);
      file_put(f);
      return -ENOEXEC;
    }
    interp_path[ph[i].p_filesz] = '\0';
  }
  struct file *interp = NULL;
  Elf64_Ehdr ieh;
  Elf64_Phdr *iph = NULL;
  if (interp_path[0]) {
    interp = vfs_open(AT_FDCWD, interp_path, O_RDONLY, 0, &err);
    if (!interp) {
      kfree(ph);
      file_put(f);
      return err == -ENOENT ? -ENOENT : err;
    }
    r = read_ehdr(interp, &ieh, &iph);
    if (r) {
      file_put(interp);
      kfree(ph);
      file_put(f);
      return r;
    }
  }

  /* ---- point of no return ---- */
  struct mm *mm = mm_create();
  if (!mm) {
    r = -ENOMEM;
    goto out;
  }
  u64 rnd = get_random_u64();
  struct loaded_elf exe, ild = {0};
  r = load_elf(mm, f, &eh, ph, USER_EXEC_BASE_PIE + ((rnd & 0xfff) << PAGE_SHIFT), &exe);
  if (!r && interp) r = load_elf(mm, interp, &ieh, iph, mm->mmap_base - (16UL << 30) + (((rnd >> 12) & 0xfff) << PAGE_SHIFT), &ild);
  if (r) {
    mm_put(mm);
    goto out;
  }
  mm->entry = exe.entry;
  mm->brk_start = mm->brk = ALIGN_UP(exe.end, PAGE_SIZE) + (((rnd >> 24) & 0xff) << PAGE_SHIFT);
  /* stack VMA grows down on demand */
  r = map_vma(mm, mm->stack_top - 128 * 1024, mm->stack_top, VM_READ | VM_WRITE | VM_GROWSDOWN, NULL, 0, 0);
  if (!r) r = map_sigtramp(mm);
  if (r) {
    mm_put(mm);
    goto out;
  }

  struct process *p = current->proc;
  de_thread();
  struct mm *old = p->mm;
  p->mm = mm;
  switch_mm(old, mm);
  if (old) mm_put(old);

  u64 sp;
  r = build_stack(mm, a, &exe, interp ? ild.load_bias : 0, path, &sp);
  if (r) {
    /* new image is unusable: kill the process */
    kfree(ph);
    kfree(iph);
    if (interp) file_put(interp);
    file_put(f);
    force_sig_fault(SIGSEGV, SI_KERNEL, 0);
    return 0;
  }

  /* process image state */
  fdtable_close_on_exec(p->files);
  for (int s = 1; s <= NSIG; s++) {
    struct k_sigaction *ka = &p->sighand->action[s];
    if (ka->handler != SIG_IGN) ka->handler = SIG_DFL;
    ka->flags = 0;
    ka->mask = 0;
  }
  memset(&current->sigaltstack, 0, sizeof(current->sigaltstack));
  current->sigaltstack.ss_flags = SS_DISABLE;
  const char *base = strrchr(path, '/');
  strlcpy(p->comm, base ? base + 1 : path, sizeof(p->comm));
  strlcpy(current->name, p->comm, sizeof(current->name));
  {
    struct path xp = f->path;
    if (d_path(&xp, p->exe, sizeof(p->exe)) < 0) strlcpy(p->exe, path, sizeof(p->exe));
  }
  if (p->vfork_done) {
    complete(p->vfork_done);
    p->vfork_done = NULL;
  }

  /* fresh register state */
  struct pt_regs *regs = current->user_regs;
  memset(regs, 0, sizeof(*regs));
  regs->sp = sp;
  regs->pc = interp ? ild.entry : exe.entry;
  regs->pstate = PSR_MODE_EL0t;
  regs->syscallno = -1;
  memset(&current->fpsimd, 0, sizeof(current->fpsimd));
  fpsimd_load(&current->fpsimd);
  current->tpidr_el0 = 0;
  write_sysreg(tpidr_el0, 0);
  r = 0;
out:
  kfree(ph);
  kfree(iph);
  if (interp) file_put(interp);
  file_put(f);
  return r;
}

int do_execve(const char *path, char *const *argv, char *const *envp, bool kernel_args) {
  struct exec_args a = {0};
  a.buf = vmalloc(ARG_MAX);
  if (!a.buf) return -ENOMEM;
  int r = copy_strings(&a, argv, kernel_args, &a.argc);
  if (!r) r = copy_strings(&a, envp, kernel_args, &a.envc);
  if (!r && a.argc == 0) {
    /* POSIX leaves argc == 0 undefined; supply argv[0] like Linux does */
    r = prepend_args(&a, (const char *const[]){path}, 1);
  }
  if (!r) r = exec_file(path, &a, 0);
  vfree(a.buf);
  return r;
}

int kernel_execve(const char *path, const char *const *argv, const char *const *envp) {
  return do_execve(path, (char *const *)argv, (char *const *)envp, true);
}

long sys_execve(u64 upath, u64 argv, u64 envp);
long sys_execve(u64 upath, u64 argv, u64 envp) {
  int err;
  char *path = strndup_user(upath, PATH_MAX, &err);
  if (!path) return err;
  int r = do_execve(path, (char *const *)argv, (char *const *)envp, false);
  kfree(path);
  if (r == 0) return current->user_regs->regs[0]; /* keep the fresh x0 (0) */
  return r;
}
