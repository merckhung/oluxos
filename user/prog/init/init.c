/*
 * OluxOS init (PID 1)
 *
 * - runs /etc/init.conf: sysinit, once, respawn, console and shutdown actions
 *   (a respawn service exiting with status 78 is not restarted)
 * - supervises services, restarting them with exponential back-off
 * - reaps orphaned processes
 * - feeds the hardware watchdog (/dev/watchdog) so that a hung userspace
 *   resets the board
 * - shutdown/reboot on the BusyBox signal conventions:
 *     SIGTERM: reboot, SIGUSR2: power off, SIGUSR1: halt, SIGINT: reboot
 */
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define MAX_ENTRIES 32
#define MIN_BACKOFF_MS 500
#define MAX_BACKOFF_MS 60000
#define STABLE_MS 10000
/* A service exiting with this status (EX_CONFIG) is never restarted. */
#define EXIT_NOT_APPLICABLE 78
#define WDT_PET_MS 5000

enum action { A_SYSINIT, A_ONCE, A_RESPAWN, A_CONSOLE, A_SHUTDOWN };

struct entry {
  enum action action;
  char cmd[256];
  pid_t pid;
  long long started_ms;
  long long next_start_ms;
  int backoff_ms;
  int restarts;
};

static struct entry entries[MAX_ENTRIES];
static int nentries;
static volatile sig_atomic_t shutdown_req; /* 0, or RB_* command */
static int wdt_fd = -1;

static long long now_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec * 1000LL + ts.tv_nsec / 1000000;
}

static void klog(const char *fmt, ...) {
  char buf[256];
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(buf + 9, sizeof(buf) - 9, fmt, ap);
  va_end(ap);
  memcpy(buf, "<6>init: ", 9);
  int fd = open("/dev/kmsg", O_WRONLY | O_CLOEXEC);
  if (fd >= 0) {
    if (write(fd, buf, 9 + (n < (int)sizeof(buf) - 9 ? n : (int)sizeof(buf) - 10)) < 0) {
    }
    close(fd);
  } else {
    fprintf(stderr, "%s\n", buf + 3);
  }
}

static void parse_config(void) {
  FILE *f = fopen("/etc/init.conf", "r");
  if (!f) {
    klog("no /etc/init.conf, starting a console shell");
    entries[0].action = A_CONSOLE;
    strcpy(entries[0].cmd, "/bin/sh -l");
    nentries = 1;
    return;
  }
  char line[300];
  while (fgets(line, sizeof(line), f) && nentries < MAX_ENTRIES) {
    char *s = line;
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '#' || *s == '\n' || !*s) continue;
    char *nl = strchr(s, '\n');
    if (nl) *nl = '\0';
    char *sp = s + strcspn(s, " \t");
    if (!*sp) continue;
    *sp++ = '\0';
    while (*sp == ' ' || *sp == '\t') sp++;
    struct entry *e = &entries[nentries];
    if (!strcmp(s, "sysinit"))
      e->action = A_SYSINIT;
    else if (!strcmp(s, "once"))
      e->action = A_ONCE;
    else if (!strcmp(s, "respawn"))
      e->action = A_RESPAWN;
    else if (!strcmp(s, "console"))
      e->action = A_CONSOLE;
    else if (!strcmp(s, "shutdown"))
      e->action = A_SHUTDOWN;
    else {
      klog("unknown action '%s' in /etc/init.conf", s);
      continue;
    }
    snprintf(e->cmd, sizeof(e->cmd), "%s", sp);
    e->backoff_ms = MIN_BACKOFF_MS;
    nentries++;
  }
  fclose(f);
}

static pid_t spawn(const char *cmd, int console) {
  sigset_t all;
  sigfillset(&all);
  sigset_t old;
  sigprocmask(SIG_BLOCK, &all, &old);
  pid_t pid = fork();
  if (pid != 0) {
    sigprocmask(SIG_SETMASK, &old, NULL);
    return pid;
  }
  /* child */
  for (int s = 1; s < NSIG; s++) signal(s, SIG_DFL);
  sigprocmask(SIG_SETMASK, &old, NULL);
  setsid();
  int fd = open("/dev/console", O_RDWR);
  if (fd >= 0) {
    if (console) ioctl(fd, TIOCSCTTY, 1);
    dup2(fd, 0);
    dup2(fd, 1);
    dup2(fd, 2);
    if (fd > 2) close(fd);
  }
  if (console) {
    const char *home = "/root";
    if (chdir(home) < 0 && chdir("/") < 0) {
    }
    setenv("HOME", home, 1);
  }
  setenv("PATH", "/sbin:/usr/sbin:/bin:/usr/bin", 1);
  execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
  _exit(127);
}

static void run_and_wait(const char *cmd) {
  pid_t pid = spawn(cmd, 0);
  if (pid < 0) return;
  int st;
  while (waitpid(pid, &st, 0) < 0 && errno == EINTR) {
  }
}

static void on_signal(int sig) {
  switch (sig) {
    case SIGTERM:
    case SIGINT:
      shutdown_req = RB_AUTOBOOT;
      break;
    case SIGUSR2:
      shutdown_req = RB_POWER_OFF;
      break;
    case SIGUSR1:
      shutdown_req = RB_HALT_SYSTEM;
      break;
    default:
      break;
  }
}

/* Supervise the system with /dev/watchdog unless the kernel command line
 * says init.watchdog=0 (e.g. to hand the watchdog to another daemon). */
static void wdt_open(void) {
  char cmdline[1024] = "";
  int fd = open("/proc/cmdline", O_RDONLY | O_CLOEXEC);
  if (fd >= 0) {
    ssize_t n = read(fd, cmdline, sizeof(cmdline) - 1);
    cmdline[n > 0 ? n : 0] = 0;
    close(fd);
  }
  if (strstr(cmdline, "init.watchdog=0")) {
    klog("watchdog supervision disabled on the command line");
    return;
  }
  wdt_fd = open("/dev/watchdog", O_WRONLY | O_CLOEXEC);
  if (wdt_fd >= 0) klog("watchdog armed");
}

static void wdt_pet(void) {
  if (wdt_fd >= 0 && write(wdt_fd, "k", 1) < 0) {
  }
}

static void start_entry(struct entry *e) {
  e->pid = spawn(e->cmd, e->action == A_CONSOLE);
  e->started_ms = now_ms();
  if (e->pid < 0) {
    klog("failed to start '%s': %s", e->cmd, strerror(errno));
    e->pid = 0;
    e->next_start_ms = now_ms() + e->backoff_ms;
  }
}

static void reap(void) {
  for (;;) {
    int st;
    pid_t pid = waitpid(-1, &st, WNOHANG);
    if (pid <= 0) return;
    for (int i = 0; i < nentries; i++) {
      struct entry *e = &entries[i];
      if (e->pid != pid) continue;
      e->pid = 0;
      if (e->action != A_RESPAWN && e->action != A_CONSOLE) break;
      if (WIFEXITED(st) && WEXITSTATUS(st) == EXIT_NOT_APPLICABLE) {
        klog("'%s' (pid %d) is not applicable here (status %d); not restarting", e->cmd, pid, EXIT_NOT_APPLICABLE);
        e->action = A_ONCE;
        break;
      }
      long long lived = now_ms() - e->started_ms;
      if (lived >= STABLE_MS)
        e->backoff_ms = MIN_BACKOFF_MS;
      else if (e->backoff_ms < MAX_BACKOFF_MS)
        e->backoff_ms *= 2;
      if (e->backoff_ms > MAX_BACKOFF_MS) e->backoff_ms = MAX_BACKOFF_MS;
      e->next_start_ms = now_ms() + (e->action == A_CONSOLE && lived >= STABLE_MS ? 0 : e->backoff_ms);
      e->restarts++;
      if (WIFSIGNALED(st))
        klog("'%s' (pid %d) killed by signal %d; restarting in %d ms", e->cmd, pid, WTERMSIG(st),
             (int)(e->next_start_ms - now_ms()));
      else if (e->action == A_RESPAWN)
        klog("'%s' (pid %d) exited with status %d; restarting in %d ms", e->cmd, pid, WEXITSTATUS(st),
             (int)(e->next_start_ms - now_ms()));
      break;
    }
  }
}

static void do_shutdown(int how) {
  klog("%s requested", how == RB_AUTOBOOT ? "reboot" : how == RB_POWER_OFF ? "power-off" : "halt");
  for (int i = 0; i < nentries; i++) entries[i].action = entries[i].action == A_SHUTDOWN ? A_SHUTDOWN : A_ONCE;
  for (int i = 0; i < nentries; i++)
    if (entries[i].action == A_SHUTDOWN) run_and_wait(entries[i].cmd);
  kill(-1, SIGTERM);
  for (int i = 0; i < 20; i++) {
    usleep(100000);
    while (waitpid(-1, NULL, WNOHANG) > 0) {
    }
  }
  kill(-1, SIGKILL);
  while (waitpid(-1, NULL, WNOHANG) > 0) {
  }
  sync();
  if (wdt_fd >= 0) {
    if (write(wdt_fd, "V", 1) < 0) { /* magic close: disarm */
    }
    close(wdt_fd);
  }
  reboot(how);
  for (;;) pause();
}

int main(int argc, char **argv) {
  if (getpid() != 1) {
    /* invoked by a user: behave like "init <runlevel>" is unsupported */
    fprintf(stderr, "init: must be run as PID 1\n");
    return 1;
  }
  struct sigaction sa = {0};
  sa.sa_handler = on_signal;
  sigemptyset(&sa.sa_mask);
  static const int shutdown_sigs[] = {SIGTERM, SIGINT, SIGUSR1, SIGUSR2};
  for (unsigned i = 0; i < sizeof(shutdown_sigs) / sizeof(shutdown_sigs[0]); i++)
    sigaction(shutdown_sigs[i], &sa, NULL);
  sa.sa_handler = SIG_DFL;
  sa.sa_flags = SA_NOCLDSTOP;
  sigaction(SIGCHLD, &sa, NULL);
  signal(SIGHUP, SIG_IGN);
  signal(SIGTSTP, SIG_IGN);
  signal(SIGTTOU, SIG_IGN);
  signal(SIGTTIN, SIG_IGN);

  umask(022);
  setsid();
  klog("OluxOS init starting");
  wdt_open();
  parse_config();

  for (int i = 0; i < nentries; i++)
    if (entries[i].action == A_SYSINIT) {
      wdt_pet();
      run_and_wait(entries[i].cmd);
    }
  for (int i = 0; i < nentries; i++)
    if (entries[i].action == A_ONCE || entries[i].action == A_RESPAWN || entries[i].action == A_CONSOLE)
      start_entry(&entries[i]);

  sigset_t block, empty;
  sigemptyset(&block);
  sigaddset(&block, SIGCHLD);
  sigemptyset(&empty);
  sigprocmask(SIG_BLOCK, &block, NULL);
  for (;;) {
    if (shutdown_req) do_shutdown(shutdown_req);
    reap();
    long long now = now_ms();
    long long wake = now + WDT_PET_MS;
    for (int i = 0; i < nentries; i++) {
      struct entry *e = &entries[i];
      if ((e->action == A_RESPAWN || e->action == A_CONSOLE) && e->pid == 0) {
        if (e->next_start_ms <= now)
          start_entry(e);
        else if (e->next_start_ms < wake)
          wake = e->next_start_ms;
      }
    }
    wdt_pet();
    /* sleep until a child exits, a signal arrives or a timer is due */
    struct timespec ts = {(wake - now) / 1000, ((wake - now) % 1000) * 1000000};
    if (ts.tv_sec < 0) ts.tv_sec = ts.tv_nsec = 0;
    sigset_t w;
    sigemptyset(&w);
    sigaddset(&w, SIGCHLD);
    siginfo_t si;
    sigprocmask(SIG_SETMASK, &empty, NULL);
    sigprocmask(SIG_BLOCK, &block, NULL);
    /* raw syscall: musl's sigtimedwait() retries on EINTR, which would hide
     * a shutdown request whose handler just ran */
    syscall(SYS_rt_sigtimedwait, &w, &si, &ts, _NSIG / 8);
  }
}
