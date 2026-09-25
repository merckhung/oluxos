#ifndef OLUX_PROCESS_H
#define OLUX_PROCESS_H

#include <olux/fs.h>
#include <olux/list.h>
#include <olux/mmu_context.h>
#include <olux/sched.h>
#include <olux/signal.h>
#include <olux/time.h>

struct mm;
struct tty;

struct sighand {
  atomic_t refcount;
  struct k_sigaction action[NSIG];
};

struct cred {
  uid_t uid, euid, suid;
  gid_t gid, egid, sgid;
  int ngroups;
  gid_t groups[16];
};

struct rlimit64 {
  u64 cur, max;
};
#define RLIMIT_CPU 0
#define RLIMIT_FSIZE 1
#define RLIMIT_DATA 2
#define RLIMIT_STACK 3
#define RLIMIT_CORE 4
#define RLIMIT_RSS 5
#define RLIMIT_NPROC 6
#define RLIMIT_NOFILE 7
#define RLIMIT_MEMLOCK 8
#define RLIMIT_AS 9
#define RLIM_NLIMITS 16
#define RLIM_INFINITY (~0ULL)

#define PROC_ALIVE 0
#define PROC_ZOMBIE 1

struct process {
  int pid, pgid, sid;
  struct process *parent;
  struct list_head children;
  struct list_head sibling;
  struct list_head threads;
  int nr_threads;
  struct list_head all_link;
  atomic_t refcount;

  struct mm *mm;
  struct fdtable *files;
  struct path cwd, root;
  mode_t umask;
  struct cred cred;
  struct sighand *sighand;
  sigset_t shared_pending;
  siginfo_t shared_info[NSIG];
  struct tty *tty; /* controlling terminal */
  struct rlimit64 rlim[RLIM_NLIMITS];

  int state;
  bool exiting;
  int exit_code;       /* wait status */
  bool group_stop;     /* stopped by SIGSTOP/SIGTSTP */
  bool stop_reported, cont_reported;
  int stop_sig;
  struct wait_queue child_wait; /* this process waits for children here */
  struct completion *vfork_done;

  char comm[16];
  char exe[128];
  u64 start_time;
  u64 utime, stime, cutime, cstime;
  u64 min_flt, maj_flt;

  /* ITIMER_REAL / alarm() */
  struct ktimer alarm_timer;
  u64 alarm_interval;
  u64 alarm_expires;
};

#define current_proc (current->proc)

extern struct list_head all_processes;
extern spinlock_t procs_lock;
extern struct process *init_process;

void process_init(void);
struct process *process_find(int pid);
void process_get(struct process *p);
void process_put(struct process *p);
struct process *process_alloc(void);

long do_fork(u64 flags, u64 newsp, u64 parent_tid, u64 tls, u64 child_tid, struct pt_regs *regs);
void do_exit(int code) __noreturn;       /* this thread */
void do_group_exit(int code) __noreturn; /* whole process */
long do_wait4(int pid, u64 status_uptr, int options, u64 rusage_uptr);
int do_execve(const char *path, char *const *argv, char *const *envp, bool kernel_args);
int kernel_execve(const char *path, const char *const *argv, const char *const *envp);

/* signals */
int send_signal_process(struct process *p, int sig, const siginfo_t *info);
int send_signal_thread(struct thread *t, int sig, const siginfo_t *info);
int kill_pgrp(int pgid, int sig, const siginfo_t *info);
void force_sig_fault(int sig, int code, u64 addr);
bool signal_pending(struct thread *t);
void do_signal(struct pt_regs *regs);
void signal_wake(struct thread *t);
void process_stop_all(struct process *p, int sig);
void process_continue(struct process *p);
bool is_orphaned_pgrp(int pgid);

/* credentials */
static inline bool capable_root(void) { return current_proc->cred.euid == 0; }

#endif
