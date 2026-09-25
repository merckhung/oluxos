/*
 * olux-selftest: in-system regression tests for kernel semantics.
 * Output is TAP ("ok N - name" / "not ok N - name"); exit status is the
 * number of failures. Run with a test name to run a single test.
 */
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <sched.h>
#include <setjmp.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/random.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static int ntest, nfail;
static char failmsg[256];

#define CHECK(cond)                                                                   \
  do {                                                                                \
    if (!(cond)) {                                                                    \
      snprintf(failmsg, sizeof(failmsg), "%s:%d: %s (errno %d)", __func__, __LINE__, #cond, errno); \
      return 1;                                                                       \
    }                                                                                 \
  } while (0)

static long long now_ns(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

/* ---------------- process ---------------- */

static int t_fork_wait(void) {
  pid_t p = fork();
  CHECK(p >= 0);
  if (p == 0) _exit(42);
  int st;
  CHECK(waitpid(p, &st, 0) == p);
  CHECK(WIFEXITED(st) && WEXITSTATUS(st) == 42);
  CHECK(waitpid(-1, &st, WNOHANG) == -1 && errno == ECHILD);
  return 0;
}

static int t_cow(void) {
  static int shared_val = 1;
  char *heap = malloc(1 << 20);
  CHECK(heap);
  memset(heap, 'a', 1 << 20);
  pid_t p = fork();
  CHECK(p >= 0);
  if (p == 0) {
    shared_val = 2;
    memset(heap, 'b', 1 << 20);
    _exit(heap[12345] == 'b' && shared_val == 2 ? 0 : 1);
  }
  int st;
  waitpid(p, &st, 0);
  CHECK(WIFEXITED(st) && WEXITSTATUS(st) == 0);
  CHECK(shared_val == 1 && heap[12345] == 'a');
  free(heap);
  return 0;
}

static int t_vfork_exec(void) {
  pid_t p = vfork();
  CHECK(p >= 0);
  if (p == 0) {
    execl("/bin/true", "true", (char *)NULL);
    _exit(99);
  }
  int st;
  CHECK(waitpid(p, &st, 0) == p && WIFEXITED(st) && WEXITSTATUS(st) == 0);
  return 0;
}

static int t_exec_script(void) {
  int fd = open("/tmp/st_script.sh", O_WRONLY | O_CREAT | O_TRUNC, 0755);
  CHECK(fd >= 0);
  const char *s = "#!/bin/sh\nexit 7\n";
  CHECK(write(fd, s, strlen(s)) == (ssize_t)strlen(s));
  close(fd);
  pid_t p = fork();
  if (p == 0) {
    execl("/tmp/st_script.sh", "st_script.sh", (char *)NULL);
    _exit(99);
  }
  int st;
  waitpid(p, &st, 0);
  unlink("/tmp/st_script.sh");
  CHECK(WIFEXITED(st) && WEXITSTATUS(st) == 7);
  return 0;
}

static int t_exec_enoent(void) {
  char *argv[] = {"nope", NULL};
  CHECK(execv("/does/not/exist", argv) == -1 && errno == ENOENT);
  return 0;
}

static int t_segv_kills_child(void) {
  pid_t p = fork();
  if (p == 0) {
    volatile uintptr_t addr = 8;
    *(volatile int *)addr = 1;
    _exit(0);
  }
  int st;
  waitpid(p, &st, 0);
  CHECK(WIFSIGNALED(st) && WTERMSIG(st) == SIGSEGV);
  return 0;
}

static int t_kernel_ptr_rejected(void) {
  /* the kernel must not read or write kernel memory on our behalf */
  int fd = open("/dev/null", O_WRONLY);
  CHECK(fd >= 0);
  CHECK(write(fd, (void *)0xffffff8040000000UL, 16) == -1 && errno == EFAULT);
  close(fd);
  fd = open("/dev/zero", O_RDONLY);
  CHECK(read(fd, (void *)0xffffffff80000000UL, 16) == -1 && errno == EFAULT);
  close(fd);
  return 0;
}

static int t_readonly_text_protected(void) {
  /* read() into our own read-only code must fail, not patch it */
  int fd = open("/dev/zero", O_RDONLY);
  CHECK(read(fd, (void *)t_readonly_text_protected, 4) == -1 && errno == EFAULT);
  close(fd);
  return 0;
}

/* ---------------- signals ---------------- */

static volatile sig_atomic_t got_sig;
static void h_usr1(int s) { got_sig = s; }

static int t_signal_handler(void) {
  struct sigaction sa = {0};
  sa.sa_handler = h_usr1;
  CHECK(sigaction(SIGUSR1, &sa, NULL) == 0);
  got_sig = 0;
  kill(getpid(), SIGUSR1);
  CHECK(got_sig == SIGUSR1);
  signal(SIGUSR1, SIG_DFL);
  return 0;
}

static int t_signal_mask(void) {
  signal(SIGUSR1, h_usr1);
  sigset_t s, old;
  sigemptyset(&s);
  sigaddset(&s, SIGUSR1);
  sigprocmask(SIG_BLOCK, &s, &old);
  got_sig = 0;
  kill(getpid(), SIGUSR1);
  CHECK(got_sig == 0);
  sigset_t pend;
  sigpending(&pend);
  CHECK(sigismember(&pend, SIGUSR1));
  sigprocmask(SIG_SETMASK, &old, NULL);
  CHECK(got_sig == SIGUSR1);
  signal(SIGUSR1, SIG_DFL);
  return 0;
}

static void h_alrm(int s) { got_sig = s; }

static int t_eintr_and_alarm(void) {
  struct sigaction sa = {0};
  sa.sa_handler = h_alrm; /* no SA_RESTART */
  sigaction(SIGALRM, &sa, NULL);
  int p[2];
  CHECK(pipe(p) == 0);
  got_sig = 0;
  struct itimerval it = {{0, 0}, {0, 100000}};
  setitimer(ITIMER_REAL, &it, NULL);
  char c;
  ssize_t r = read(p[0], &c, 1);
  CHECK(r == -1 && errno == EINTR && got_sig == SIGALRM);
  close(p[0]);
  close(p[1]);
  signal(SIGALRM, SIG_DFL);
  return 0;
}

static int t_sa_restart(void) {
  struct sigaction sa = {0};
  sa.sa_handler = h_alrm;
  sa.sa_flags = SA_RESTART;
  sigaction(SIGALRM, &sa, NULL);
  int p[2];
  CHECK(pipe(p) == 0);
  pid_t child = fork();
  if (child == 0) {
    usleep(300000);
    if (write(p[1], "x", 1) != 1) _exit(1);
    _exit(0);
  }
  got_sig = 0;
  struct itimerval it = {{0, 0}, {0, 100000}};
  setitimer(ITIMER_REAL, &it, NULL);
  char c;
  ssize_t r = read(p[0], &c, 1); /* interrupted, then transparently restarted */
  waitpid(child, NULL, 0);
  CHECK(r == 1 && c == 'x' && got_sig == SIGALRM);
  close(p[0]);
  close(p[1]);
  signal(SIGALRM, SIG_DFL);
  return 0;
}

static sigjmp_buf jb;
static void h_segv(int s) { siglongjmp(jb, 1); }

static int t_sigsegv_handler(void) {
  signal(SIGSEGV, h_segv);
  volatile int reached = 0;
  if (sigsetjmp(jb, 1) == 0) {
    volatile uintptr_t addr = 16;
    *(volatile int *)addr = 1;
    reached = 1;
  }
  signal(SIGSEGV, SIG_DFL);
  CHECK(!reached);
  return 0;
}

static int t_sigaltstack(void) {
  stack_t ss = {0};
  ss.ss_sp = malloc(SIGSTKSZ);
  ss.ss_size = SIGSTKSZ;
  CHECK(sigaltstack(&ss, NULL) == 0);
  struct sigaction sa = {0};
  sa.sa_handler = h_usr1;
  sa.sa_flags = SA_ONSTACK;
  sigaction(SIGUSR1, &sa, NULL);
  got_sig = 0;
  raise(SIGUSR1);
  CHECK(got_sig == SIGUSR1);
  ss.ss_flags = SS_DISABLE;
  sigaltstack(&ss, NULL);
  signal(SIGUSR1, SIG_DFL);
  return 0;
}

static int t_fp_preserved_across_signal(void) {
  signal(SIGUSR1, h_usr1);
  volatile double a = 1.5, b = 2.25;
  double c = a * b;
  raise(SIGUSR1);
  double d = a * b;
  CHECK(c == d && c == 3.375);
  signal(SIGUSR1, SIG_DFL);
  return 0;
}

static int t_stop_cont(void) {
  pid_t p = fork();
  if (p == 0) {
    for (;;) pause();
  }
  int st;
  kill(p, SIGSTOP);
  CHECK(waitpid(p, &st, WUNTRACED) == p && WIFSTOPPED(st) && WSTOPSIG(st) == SIGSTOP);
  kill(p, SIGCONT);
  CHECK(waitpid(p, &st, WCONTINUED) == p && WIFCONTINUED(st));
  kill(p, SIGKILL);
  CHECK(waitpid(p, &st, 0) == p && WIFSIGNALED(st) && WTERMSIG(st) == SIGKILL);
  return 0;
}

/* ---------------- memory ---------------- */

static int t_mmap_anon(void) {
  size_t sz = 8 << 20;
  unsigned char *m = mmap(NULL, sz, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  CHECK(m != MAP_FAILED);
  CHECK(m[0] == 0 && m[sz - 1] == 0);
  for (size_t i = 0; i < sz; i += 4096) m[i] = (unsigned char)i;
  CHECK(m[4096 * 5] == (unsigned char)(4096 * 5));
  CHECK(munmap(m, sz) == 0);
  return 0;
}

static int t_mprotect(void) {
  pid_t p = fork();
  if (p == 0) {
    char *m = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    m[0] = 1;
    mprotect(m, 4096, PROT_READ);
    m[0] = 2; /* must fault */
    _exit(0);
  }
  int st;
  waitpid(p, &st, 0);
  CHECK(WIFSIGNALED(st) && WTERMSIG(st) == SIGSEGV);
  return 0;
}

static int t_mmap_shared_fork(void) {
  int *m = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
  CHECK(m != MAP_FAILED);
  *m = 0;
  pid_t p = fork();
  if (p == 0) {
    *m = 1234;
    _exit(0);
  }
  waitpid(p, NULL, 0);
  CHECK(*m == 1234);
  munmap(m, 4096);
  return 0;
}

static int t_mmap_file(void) {
  int fd = open("/tmp/st_map", O_RDWR | O_CREAT | O_TRUNC, 0644);
  CHECK(fd >= 0);
  char buf[8192];
  for (int i = 0; i < 8192; i++) buf[i] = (char)(i * 7);
  CHECK(write(fd, buf, sizeof(buf)) == sizeof(buf));
  char *m = mmap(NULL, 8192, PROT_READ, MAP_PRIVATE, fd, 0);
  CHECK(m != MAP_FAILED);
  CHECK(!memcmp(m, buf, 8192));
  munmap(m, 8192);
  close(fd);
  unlink("/tmp/st_map");
  return 0;
}

static int t_stack_growth(void) {
  pid_t p = fork();
  if (p == 0) {
    volatile char big[2 << 20];
    for (size_t i = 0; i < sizeof(big); i += 4096) big[i] = 1;
    _exit(big[4096] == 1 ? 0 : 1);
  }
  int st;
  waitpid(p, &st, 0);
  CHECK(WIFEXITED(st) && WEXITSTATUS(st) == 0);
  return 0;
}

static int t_brk_malloc(void) {
  void *ptrs[256];
  for (int i = 0; i < 256; i++) {
    ptrs[i] = malloc(1000 + i * 37);
    CHECK(ptrs[i]);
    memset(ptrs[i], i, 1000 + i * 37);
  }
  for (int i = 0; i < 256; i++) free(ptrs[i]);
  void *big = malloc(16 << 20);
  CHECK(big);
  memset(big, 1, 16 << 20);
  free(big);
  return 0;
}

/* ---------------- threads ---------------- */

static atomic_int counter;
static pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
static long plain_counter;

static void *worker(void *arg) {
  for (int i = 0; i < 20000; i++) {
    atomic_fetch_add(&counter, 1);
    pthread_mutex_lock(&mtx);
    plain_counter++;
    pthread_mutex_unlock(&mtx);
  }
  return arg;
}

static int t_pthreads(void) {
  pthread_t th[4];
  counter = 0;
  plain_counter = 0;
  for (long i = 0; i < 4; i++) CHECK(pthread_create(&th[i], NULL, worker, (void *)i) == 0);
  for (long i = 0; i < 4; i++) {
    void *ret;
    CHECK(pthread_join(th[i], &ret) == 0 && ret == (void *)i);
  }
  CHECK(counter == 80000 && plain_counter == 80000);
  return 0;
}

static __thread int tls_var = 5;
static void *tls_worker(void *arg) {
  tls_var = (int)(long)arg;
  usleep(10000);
  return (void *)(long)tls_var;
}

static int t_tls(void) {
  pthread_t a, b;
  pthread_create(&a, NULL, tls_worker, (void *)11);
  pthread_create(&b, NULL, tls_worker, (void *)22);
  void *ra, *rb;
  pthread_join(a, &ra);
  pthread_join(b, &rb);
  CHECK((long)ra == 11 && (long)rb == 22 && tls_var == 5);
  return 0;
}

static pthread_cond_t cv = PTHREAD_COND_INITIALIZER;
static int ready;
static void *cv_worker(void *arg) {
  pthread_mutex_lock(&mtx);
  ready = 1;
  pthread_cond_signal(&cv);
  pthread_mutex_unlock(&mtx);
  return NULL;
}

static int t_condvar(void) {
  pthread_t t;
  ready = 0;
  pthread_mutex_lock(&mtx);
  pthread_create(&t, NULL, cv_worker, NULL);
  while (!ready) pthread_cond_wait(&cv, &mtx);
  pthread_mutex_unlock(&mtx);
  pthread_join(t, NULL);
  CHECK(ready == 1);
  return 0;
}

static void *fp_worker(void *arg) {
  double x = (double)(long)arg;
  double acc = 0;
  for (int i = 0; i < 200000; i++) acc += x * 0.5;
  return (void *)(long)(acc == x * 0.5 * 200000);
}

static int t_fp_context_switch(void) {
  pthread_t th[4];
  for (long i = 0; i < 4; i++) pthread_create(&th[i], NULL, fp_worker, (void *)(i + 1));
  for (int i = 0; i < 4; i++) {
    void *r;
    pthread_join(th[i], &r);
    CHECK(r == (void *)1);
  }
  return 0;
}

/* ---------------- files ---------------- */

static int t_file_rw(void) {
  int fd = open("/tmp/st_file", O_RDWR | O_CREAT | O_TRUNC, 0600);
  CHECK(fd >= 0);
  CHECK(write(fd, "hello world", 11) == 11);
  CHECK(lseek(fd, 6, SEEK_SET) == 6);
  char b[16] = {0};
  CHECK(read(fd, b, 5) == 5 && !memcmp(b, "world", 5));
  CHECK(pwrite(fd, "W", 1, 6) == 1);
  CHECK(pread(fd, b, 11, 0) == 11 && !memcmp(b, "hello World", 11));
  struct stat st;
  CHECK(fstat(fd, &st) == 0 && st.st_size == 11 && S_ISREG(st.st_mode));
  CHECK(ftruncate(fd, 5) == 0 && fstat(fd, &st) == 0 && st.st_size == 5);
  close(fd);
  CHECK(unlink("/tmp/st_file") == 0);
  CHECK(access("/tmp/st_file", F_OK) == -1 && errno == ENOENT);
  return 0;
}

static int t_dirs(void) {
  CHECK(mkdir("/tmp/st_dir", 0755) == 0);
  CHECK(mkdir("/tmp/st_dir", 0755) == -1 && errno == EEXIST);
  int fd = open("/tmp/st_dir/a", O_CREAT | O_WRONLY, 0644);
  CHECK(fd >= 0);
  close(fd);
  CHECK(rename("/tmp/st_dir/a", "/tmp/st_dir/b") == 0);
  CHECK(rmdir("/tmp/st_dir") == -1 && errno == ENOTEMPTY);
  CHECK(symlink("b", "/tmp/st_dir/link") == 0);
  char buf[16] = {0};
  CHECK(readlink("/tmp/st_dir/link", buf, sizeof(buf)) == 1 && buf[0] == 'b');
  CHECK(link("/tmp/st_dir/b", "/tmp/st_dir/hard") == 0);
  struct stat st;
  CHECK(stat("/tmp/st_dir/link", &st) == 0 && st.st_nlink == 2);
  CHECK(chdir("/tmp/st_dir") == 0);
  char cwd[64];
  CHECK(getcwd(cwd, sizeof(cwd)) && !strcmp(cwd, "/tmp/st_dir"));
  CHECK(chdir("..") == 0 && getcwd(cwd, sizeof(cwd)) && !strcmp(cwd, "/tmp"));
  chdir("/");
  unlink("/tmp/st_dir/link");
  unlink("/tmp/st_dir/hard");
  unlink("/tmp/st_dir/b");
  CHECK(rmdir("/tmp/st_dir") == 0);
  return 0;
}

static int t_dup_cloexec(void) {
  int fd = open("/dev/null", O_RDONLY | O_CLOEXEC);
  CHECK(fd >= 0);
  CHECK(fcntl(fd, F_GETFD) == FD_CLOEXEC);
  int d = dup(fd);
  CHECK(d > fd && fcntl(d, F_GETFD) == 0);
  CHECK(dup2(fd, 50) == 50);
  close(fd);
  close(d);
  close(50);
  CHECK(fcntl(50, F_GETFD) == -1 && errno == EBADF);
  return 0;
}

static int t_pipe_poll(void) {
  int p[2];
  CHECK(pipe(p) == 0);
  struct pollfd pf = {p[0], POLLIN, 0};
  CHECK(poll(&pf, 1, 0) == 0);
  CHECK(write(p[1], "abc", 3) == 3);
  CHECK(poll(&pf, 1, 100) == 1 && (pf.revents & POLLIN));
  char b[4];
  CHECK(read(p[0], b, 4) == 3);
  close(p[1]);
  CHECK(read(p[0], b, 4) == 0); /* EOF */
  close(p[0]);
  CHECK(pipe(p) == 0);
  signal(SIGPIPE, SIG_IGN);
  close(p[0]);
  CHECK(write(p[1], "x", 1) == -1 && errno == EPIPE);
  close(p[1]);
  signal(SIGPIPE, SIG_DFL);
  return 0;
}

static int t_poll_timeout(void) {
  int p[2];
  pipe(p);
  struct pollfd pf = {p[0], POLLIN, 0};
  long long t0 = now_ns();
  CHECK(poll(&pf, 1, 200) == 0);
  long long dt = now_ns() - t0;
  CHECK(dt >= 190000000LL && dt < 600000000LL);
  close(p[0]);
  close(p[1]);
  return 0;
}

static int t_eventfd(void) {
  int fd = eventfd(3, 0);
  CHECK(fd >= 0);
  uint64_t v = 4;
  CHECK(write(fd, &v, 8) == 8);
  CHECK(read(fd, &v, 8) == 8 && v == 7);
  close(fd);
  return 0;
}

static int t_readv_writev(void) {
  int p[2];
  pipe(p);
  struct iovec w[2] = {{"ab", 2}, {"cde", 3}};
  CHECK(writev(p[1], w, 2) == 5);
  char x[2], y[3];
  struct iovec r[2] = {{x, 2}, {y, 3}};
  CHECK(readv(p[0], r, 2) == 5 && !memcmp(x, "ab", 2) && !memcmp(y, "cde", 3));
  close(p[0]);
  close(p[1]);
  return 0;
}

static int t_proc(void) {
  char buf[256];
  int fd = open("/proc/self/stat", O_RDONLY);
  CHECK(fd >= 0);
  ssize_t n = read(fd, buf, sizeof(buf) - 1);
  close(fd);
  CHECK(n > 0);
  buf[n] = 0;
  CHECK(atoi(buf) == getpid());
  CHECK(readlink("/proc/self/exe", buf, sizeof(buf)) > 0);
  return 0;
}

static int t_devices(void) {
  char b[8];
  int fd = open("/dev/zero", O_RDONLY);
  CHECK(read(fd, b, 8) == 8 && b[0] == 0 && b[7] == 0);
  close(fd);
  fd = open("/dev/urandom", O_RDONLY);
  uint64_t r1 = 0, r2 = 0;
  CHECK(read(fd, &r1, 8) == 8 && read(fd, &r2, 8) == 8 && r1 != r2);
  close(fd);
  CHECK(getrandom(b, 8, 0) == 8);
  return 0;
}

/* ---------------- time / sched ---------------- */

static int t_nanosleep(void) {
  long long t0 = now_ns();
  struct timespec ts = {0, 50000000};
  CHECK(nanosleep(&ts, NULL) == 0);
  long long dt = now_ns() - t0;
  CHECK(dt >= 49000000LL && dt < 200000000LL);
  return 0;
}

static int t_clocks(void) {
  struct timespec a, b;
  CHECK(clock_gettime(CLOCK_MONOTONIC, &a) == 0);
  CHECK(clock_gettime(CLOCK_MONOTONIC, &b) == 0);
  CHECK(b.tv_sec > a.tv_sec || (b.tv_sec == a.tv_sec && b.tv_nsec >= a.tv_nsec));
  CHECK(clock_gettime(CLOCK_REALTIME, &a) == 0);
  CHECK(clock_getres(CLOCK_MONOTONIC, &a) == 0);
  struct timeval tv;
  CHECK(gettimeofday(&tv, NULL) == 0);
  return 0;
}

static int t_preemption(void) {
  /* a CPU-bound child must not starve its parent */
  int ncpu = (int)sysconf(_SC_NPROCESSORS_ONLN);
  pid_t kids[16];
  int n = ncpu + 1 > 16 ? 16 : ncpu + 1;
  for (int i = 0; i < n; i++) {
    kids[i] = fork();
    if (kids[i] == 0)
      for (;;) {
      }
  }
  long long t0 = now_ns();
  usleep(100000);
  long long dt = now_ns() - t0;
  for (int i = 0; i < n; i++) kill(kids[i], SIGKILL);
  for (int i = 0; i < n; i++) waitpid(kids[i], NULL, 0);
  CHECK(dt < 1000000000LL);
  return 0;
}

static int t_rt_priority(void) {
  /* musl stubs sched_setscheduler(); use the per-thread syscall */
  struct sched_param sp = {.sched_priority = 10};
  CHECK(syscall(SYS_sched_setscheduler, 0, SCHED_FIFO, &sp) == 0);
  CHECK(syscall(SYS_sched_getscheduler, 0) == SCHED_FIFO);
  CHECK(pthread_setschedparam(pthread_self(), SCHED_RR, &sp) == 0);
  int pol;
  CHECK(pthread_getschedparam(pthread_self(), &pol, &sp) == 0 && pol == SCHED_RR && sp.sched_priority == 10);
  sp.sched_priority = 0;
  CHECK(syscall(SYS_sched_setscheduler, 0, SCHED_OTHER, &sp) == 0);
  return 0;
}

static int t_uname_rlimit(void) {
  struct utsname u;
  CHECK(uname(&u) == 0 && !strcmp(u.sysname, "OluxOS") && !strcmp(u.machine, "aarch64"));
  struct rlimit rl;
  CHECK(getrlimit(RLIMIT_NOFILE, &rl) == 0 && rl.rlim_cur >= 64);
  return 0;
}

static int t_process_groups(void) {
  pid_t p = fork();
  if (p == 0) {
    if (setpgid(0, 0) != 0) _exit(1);
    _exit(getpgrp() == getpid() ? 0 : 2);
  }
  int st;
  waitpid(p, &st, 0);
  CHECK(WIFEXITED(st) && WEXITSTATUS(st) == 0);
  return 0;
}

static int t_many_processes(void) {
  for (int round = 0; round < 50; round++) {
    pid_t p = fork();
    CHECK(p >= 0);
    if (p == 0) _exit(round & 0x7f);
    int st;
    CHECK(waitpid(p, &st, 0) == p && WEXITSTATUS(st) == (round & 0x7f));
  }
  return 0;
}

struct test {
  const char *name;
  int (*fn)(void);
};

static const struct test tests[] = {
    {"fork_wait", t_fork_wait},
    {"cow", t_cow},
    {"vfork_exec", t_vfork_exec},
    {"exec_script", t_exec_script},
    {"exec_enoent", t_exec_enoent},
    {"segv_kills_child", t_segv_kills_child},
    {"kernel_ptr_rejected", t_kernel_ptr_rejected},
    {"readonly_text_protected", t_readonly_text_protected},
    {"signal_handler", t_signal_handler},
    {"signal_mask", t_signal_mask},
    {"eintr_and_alarm", t_eintr_and_alarm},
    {"sa_restart", t_sa_restart},
    {"sigsegv_handler", t_sigsegv_handler},
    {"sigaltstack", t_sigaltstack},
    {"fp_preserved_across_signal", t_fp_preserved_across_signal},
    {"stop_cont", t_stop_cont},
    {"mmap_anon", t_mmap_anon},
    {"mprotect", t_mprotect},
    {"mmap_shared_fork", t_mmap_shared_fork},
    {"mmap_file", t_mmap_file},
    {"stack_growth", t_stack_growth},
    {"brk_malloc", t_brk_malloc},
    {"pthreads", t_pthreads},
    {"tls", t_tls},
    {"condvar", t_condvar},
    {"fp_context_switch", t_fp_context_switch},
    {"file_rw", t_file_rw},
    {"dirs", t_dirs},
    {"dup_cloexec", t_dup_cloexec},
    {"pipe_poll", t_pipe_poll},
    {"poll_timeout", t_poll_timeout},
    {"eventfd", t_eventfd},
    {"readv_writev", t_readv_writev},
    {"proc", t_proc},
    {"devices", t_devices},
    {"nanosleep", t_nanosleep},
    {"clocks", t_clocks},
    {"preemption", t_preemption},
    {"rt_priority", t_rt_priority},
    {"uname_rlimit", t_uname_rlimit},
    {"process_groups", t_process_groups},
    {"many_processes", t_many_processes},
};

int main(int argc, char **argv) {
  setvbuf(stdout, NULL, _IONBF, 0);
  int count = (int)(sizeof(tests) / sizeof(tests[0]));
  printf("TAP version 13\n1..%d\n", argc > 1 ? argc - 1 : count);
  for (int i = 0; i < count; i++) {
    if (argc > 1) {
      int want = 0;
      for (int a = 1; a < argc; a++)
        if (!strcmp(argv[a], tests[i].name)) want = 1;
      if (!want) continue;
    }
    ntest++;
    failmsg[0] = 0;
    errno = 0;
    int r = tests[i].fn();
    if (r) {
      nfail++;
      printf("not ok %d - %s # %s\n", ntest, tests[i].name, failmsg);
    } else {
      printf("ok %d - %s\n", ntest, tests[i].name);
    }
  }
  printf("# %d tests, %d failed\n", ntest, nfail);
  printf(nfail ? "SELFTEST FAILED\n" : "SELFTEST PASSED\n");
  return nfail;
}
