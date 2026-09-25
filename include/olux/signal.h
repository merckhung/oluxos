#ifndef OLUX_SIGNAL_H
#define OLUX_SIGNAL_H

#include <olux/types.h>

#define NSIG 64
typedef u64 sigset_t;

#define SIGHUP 1
#define SIGINT 2
#define SIGQUIT 3
#define SIGILL 4
#define SIGTRAP 5
#define SIGABRT 6
#define SIGBUS 7
#define SIGFPE 8
#define SIGKILL 9
#define SIGUSR1 10
#define SIGSEGV 11
#define SIGUSR2 12
#define SIGPIPE 13
#define SIGALRM 14
#define SIGTERM 15
#define SIGSTKFLT 16
#define SIGCHLD 17
#define SIGCONT 18
#define SIGSTOP 19
#define SIGTSTP 20
#define SIGTTIN 21
#define SIGTTOU 22
#define SIGURG 23
#define SIGXCPU 24
#define SIGXFSZ 25
#define SIGVTALRM 26
#define SIGPROF 27
#define SIGWINCH 28
#define SIGIO 29
#define SIGPWR 30
#define SIGSYS 31
#define SIGRTMIN 32

#define SIG_DFL ((u64)0)
#define SIG_IGN ((u64)1)

#define SA_NOCLDSTOP 0x00000001
#define SA_NOCLDWAIT 0x00000002
#define SA_SIGINFO 0x00000004
#define SA_ONSTACK 0x08000000
#define SA_RESTART 0x10000000
#define SA_NODEFER 0x40000000
#define SA_RESETHAND 0x80000000
#define SA_RESTORER 0x04000000

#define SIG_BLOCK 0
#define SIG_UNBLOCK 1
#define SIG_SETMASK 2

#define sigmask(sig) (1ULL << ((sig) - 1))
#define SIG_KERNEL_ONLY (sigmask(SIGKILL) | sigmask(SIGSTOP))

/* Linux kernel ABI struct sigaction (aarch64). */
struct k_sigaction {
  u64 handler;
  u64 flags;
  u64 restorer;
  sigset_t mask;
};

typedef struct {
  u64 ss_sp;
  s32 ss_flags;
  s32 pad;
  u64 ss_size;
} stack_t;
#define SS_ONSTACK 1
#define SS_DISABLE 2

/* siginfo_t (128 bytes, Linux layout for the fields we use). */
typedef struct {
  s32 si_signo;
  s32 si_errno;
  s32 si_code;
  s32 pad0;
  union {
    struct {
      s32 pid;
      u32 uid;
      s32 status;
      s32 pad;
      s64 utime, stime;
    } chld;
    struct {
      s32 pid;
      u32 uid;
    } kill;
    struct {
      u64 addr;
    } fault;
    u8 raw[112];
  };
} siginfo_t;

#define SI_USER 0
#define SI_KERNEL 0x80
#define SI_TKILL (-6)
#define SEGV_MAPERR 1
#define SEGV_ACCERR 2
#define BUS_ADRALN 1
#define ILL_ILLOPC 1
#define TRAP_BRKPT 1
#define CLD_EXITED 1
#define CLD_KILLED 2
#define CLD_DUMPED 3
#define CLD_STOPPED 5
#define CLD_CONTINUED 6

#endif
