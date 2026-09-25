/*
 * The init thread: finishes device probing, populates the root filesystem
 * and becomes the first user process (PID 1).
 */
#include <olux/device.h>
#include <olux/fs.h>
#include <olux/kernel.h>
#include <olux/mm.h>
#include <olux/process.h>
#include <olux/random.h>
#include <olux/sched.h>
#include <olux/smp.h>
#include <olux/tty.h>
#include <olux/vm.h>

void tmpfs_register(void);
void procfs_register(void);
void initrd_get(phys_addr_t *start, phys_addr_t *end);
void block_init(void);
void net_init(void);
void watchdog_kick_start(void);

static const char *init_candidates[] = {"/sbin/init", "/etc/init", "/bin/init", "/bin/sh", NULL};

static const char *cmdline_param(const char *key, char *buf, size_t size) {
  const char *cl = kernel_cmdline();
  size_t kl = strlen(key);
  for (const char *s = cl; s && *s;) {
    while (*s == ' ') s++;
    if (!strncmp(s, key, kl) && s[kl] == '=') {
      const char *v = s + kl + 1;
      size_t n = 0;
      while (v[n] && v[n] != ' ') n++;
      n = MIN(n, size - 1);
      memcpy(buf, v, n);
      buf[n] = '\0';
      return buf;
    }
    while (*s && *s != ' ') s++;
  }
  return NULL;
}

static struct process *make_init_process(void) {
  struct thread *t = current;
  struct process *p = process_alloc();
  if (!p) panic("init: out of memory");
  p->pid = p->pgid = p->sid = t->tid;
  p->parent = NULL;
  p->mm = mm_create();
  p->files = fdtable_alloc(NR_OPEN_MAX);
  p->sighand = kzalloc(sizeof(*p->sighand), 0);
  if (!p->mm || !p->files || !p->sighand) panic("init: out of memory");
  atomic_set(&p->sighand->refcount, 1);
  struct mount *root = root_mount();
  p->root.mnt = p->cwd.mnt = root;
  p->root.dentry = p->cwd.dentry = root->sb->root;
  path_get(&p->root);
  path_get(&p->cwd);
  strlcpy(p->comm, "init", sizeof(p->comm));
  ktimer_init(&p->alarm_timer, NULL, p);
  list_add_tail(&t->thread_link, &p->threads);
  p->nr_threads = 1;
  t->proc = p;
  t->kthread = false;
  list_add_tail(&p->all_link, &all_processes);
  init_process = p;
  switch_mm(NULL, p->mm);
  return p;
}

static void open_console(void) {
  struct file *f = kernel_open("/dev/console", O_RDWR, 0);
  if (IS_ERR(f)) {
    pr_warn("init: cannot open /dev/console (%ld)\n", PTR_ERR(f));
    return;
  }
  fd_install(f, 0, false);
  file_get(f);
  fd_install(f, 1, false);
  file_get(f);
  fd_install(f, 2, false);
}

int kernel_init(void *arg) {
  lock_kernel();
  random_init();
  tmpfs_register();
  procfs_register();
  vfs_init();

  /* Remaining hardware: firmware interfaces, buses, devices. */
  dt_probe_level(DRV_FIRMWARE);
  dt_probe_level(DRV_BUS);
  dt_probe_level(DRV_DEVICE);
  smp_init();

  make_init_process();

  /* Root filesystem: tmpfs populated from the initramfs. */
  phys_addr_t rs, re;
  initrd_get(&rs, &re);
  if (re > rs) {
    int r = unpack_initramfs(phys_to_virt(rs), re - rs);
    if (r) pr_err("initramfs: unpack failed: %d\n", r);
  } else {
    pr_warn("no initramfs provided\n");
  }
  vfs_mkdir(AT_FDCWD, "/dev", 0755);
  vfs_mkdir(AT_FDCWD, "/proc", 0555);
  vfs_mkdir(AT_FDCWD, "/tmp", 01777);
  vfs_mkdir(AT_FDCWD, "/sys", 0555);
  vfs_mkdir(AT_FDCWD, "/run", 0755);
  if (do_mount("devtmpfs", "/dev", "devtmpfs", SB_NOEXEC | SB_NOSUID, "mode=0755")) panic("cannot mount /dev");
  do_mount("proc", "/proc", "proc", SB_NOEXEC | SB_NOSUID | SB_NODEV, NULL);
  do_mount("tmpfs", "/tmp", "tmpfs", SB_NOSUID | SB_NODEV, "mode=1777");
  do_mount("tmpfs", "/run", "tmpfs", SB_NOSUID | SB_NODEV, "mode=0755");

  do_initcalls();
  devfs_mount_all();

  pr_info("Freeing boot memory: %llu MiB free, %d CPU(s) online\n",
          (unsigned long long)(nr_free_pages() * PAGE_SIZE >> 20), nr_cpus_online);
  open_console();

  char buf[128];
  const char *init = cmdline_param("init", buf, sizeof(buf));
  const char *envp[] = {"HOME=/", "TERM=vt100", "PATH=/sbin:/usr/sbin:/bin:/usr/bin", NULL};
  if (init) {
    const char *argv[] = {init, NULL};
    int r = kernel_execve(init, argv, envp);
    if (!r) goto run;
    pr_err("init: failed to execute %s (%d); trying defaults\n", init, r);
  }
  for (int i = 0; init_candidates[i]; i++) {
    const char *argv[] = {init_candidates[i], NULL};
    if (kernel_execve(init_candidates[i], argv, envp) == 0) goto run;
  }
  panic("No working init found. Try passing init= option to kernel.");
run:
  unlock_kernel();
  return 0; /* returns to user space through ret_from_fork */
}
