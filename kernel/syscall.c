/*
 * System call dispatch. The ABI is the Linux AArch64 generic syscall ABI
 * (x8 = number, x0-x5 = arguments, x0 = result), so unmodified musl-based
 * binaries run. OluxOS extensions are numbered from 1000.
 */
#include <olux/kernel.h>
#include <olux/process.h>
#include <olux/sched.h>
#include <olux/syscall.h>

/* Handlers have their natural prototypes; the table refers to them by
 * symbol name (extra argument registers are harmless under AAPCS64). */
#define S(n) extern char __sys_##n[] __asm__("sys_" #n)
#define SYSCALLS(X)                                                                                         \
  X(17, getcwd) X(19, eventfd2) X(23, dup) X(24, dup3) X(25, fcntl) X(29, ioctl) X(32, flock)              \
  X(33, mknodat) X(34, mkdirat) X(35, unlinkat) X(36, symlinkat) X(37, linkat) X(38, renameat)              \
  X(39, umount2) X(40, mount) X(43, statfs) X(44, fstatfs) X(45, truncate) X(46, ftruncate)                 \
  X(47, fallocate) X(48, faccessat) X(49, chdir) X(50, fchdir) X(51, chroot) X(52, fchmod) X(53, fchmodat)  \
  X(54, fchownat) X(55, fchown) X(56, openat) X(57, close) X(59, pipe2) X(61, getdents64) X(62, lseek)     \
  X(63, read) X(64, write) X(65, readv) X(66, writev) X(67, pread64) X(68, pwrite64) X(69, preadv)          \
  X(70, pwritev) X(71, sendfile) X(72, pselect6) X(73, ppoll) X(78, readlinkat) X(79, newfstatat)            \
  X(80, fstat) X(81, sync) X(82, fsync) X(83, fdatasync) X(88, utimensat) X(90, capget) X(91, capset)      \
  X(93, exit) X(94, exit_group) X(95, waitid) X(96, set_tid_address) X(98, futex) X(99, set_robust_list)   \
  X(100, get_robust_list) X(101, nanosleep) X(102, getitimer) X(103, setitimer) X(112, clock_settime) X(113, clock_gettime)       \
  X(114, clock_getres) X(115, clock_nanosleep) X(116, syslog) X(118, sched_setparam)                       \
  X(119, sched_setscheduler) X(120, sched_getscheduler) X(121, sched_getparam) X(122, sched_setaffinity)    \
  X(123, sched_getaffinity) X(124, sched_yield) X(125, sched_get_priority_max)                              \
  X(126, sched_get_priority_min) X(127, sched_rr_get_interval) X(129, kill) X(130, tkill) X(131, tgkill)   \
  X(132, sigaltstack) X(133, rt_sigsuspend) X(134, rt_sigaction) X(135, rt_sigprocmask)                    \
  X(136, rt_sigpending) X(137, rt_sigtimedwait) X(139, rt_sigreturn) X(140, setpriority)                   \
  X(141, getpriority) X(142, reboot) X(143, setregid) X(144, setgid) X(145, setreuid) X(146, setuid)        \
  X(147, setresuid) X(148, getresuid) X(149, setresgid) X(150, getresgid) X(151, setfsuid)                  \
  X(152, setfsgid) X(153, times) X(154, setpgid) X(155, getpgid) X(156, getsid) X(157, setsid)             \
  X(158, getgroups) X(159, setgroups) X(160, uname) X(161, sethostname) X(162, setdomainname)              \
  X(163, getrlimit) X(164, setrlimit) X(165, getrusage) X(166, umask) X(167, prctl) X(168, getcpu)         \
  X(169, gettimeofday) X(170, settimeofday) X(171, adjtimex) X(172, getpid) X(173, getppid) X(174, getuid) X(175, geteuid)  \
  X(176, getgid) X(177, getegid) X(178, gettid) X(179, sysinfo) X(198, socket) X(199, socketpair)          \
  X(200, bind) X(201, listen) X(202, accept) X(203, connect) X(204, getsockname) X(205, getpeername)        \
  X(206, sendto) X(207, recvfrom) X(208, setsockopt) X(209, getsockopt) X(210, shutdown) X(211, sendmsg)   \
  X(212, recvmsg) X(214, brk) X(215, munmap) X(216, mremap) X(220, clone) X(221, execve) X(222, mmap)      \
  X(223, fadvise64) X(226, mprotect) X(227, msync) X(228, mlock) X(229, munlock) X(230, mlockall)          \
  X(231, munlockall) X(232, mincore) X(233, madvise) X(242, accept4) X(260, wait4) X(261, prlimit64) X(266, clock_adjtime)       \
  X(276, renameat2) X(278, getrandom) X(279, memfd_create) X(291, statx) X(293, rseq) X(435, clone3)       \
  X(439, faccessat2)

#define DECL(n, name) S(name);
SYSCALLS(DECL)

#define ENTRY(n, name) [n] = (syscall_fn_t)(void *)__sys_##name,
static const syscall_fn_t table[NR_SYSCALLS] = {SYSCALLS(ENTRY)};

#define NAME(n, name) [n] = #name,
static const char *const names[NR_SYSCALLS] = {SYSCALLS(NAME)};

/* OluxOS extensions */
#define OLUX_SYSCALLS(X) X(0, olux_channel) X(1, olux_msg_send) X(2, olux_msg_recv) X(3, olux_sysinfo) \
  X(4, olux_watchdog)
#define ODECL(n, name) S(name);
OLUX_SYSCALLS(ODECL)
#define OENTRY(n, name) [n] = (syscall_fn_t)(void *)__sys_##name,
static const syscall_fn_t olux_table[NR_OLUX_SYSCALLS] = {OLUX_SYSCALLS(OENTRY)};
#define ONAME(n, name) [n] = #name,
static const char *const olux_names[NR_OLUX_SYSCALLS] = {OLUX_SYSCALLS(ONAME)};

bool syscall_trace;

const char *syscall_name(long nr) {
  if (nr >= 0 && nr < NR_SYSCALLS && names[nr]) return names[nr];
  if (nr >= OLUX_SYSCALL_BASE && nr < OLUX_SYSCALL_BASE + NR_OLUX_SYSCALLS && olux_names[nr - OLUX_SYSCALL_BASE])
    return olux_names[nr - OLUX_SYSCALL_BASE];
  return "?";
}

void do_syscall(struct pt_regs *regs) {
  long nr = (long)regs->regs[8];
  regs->syscallno = nr;
  regs->orig_x0 = regs->regs[0];
  syscall_fn_t fn = NULL;
  if (nr >= 0 && nr < NR_SYSCALLS) fn = table[nr];
  else if (nr >= OLUX_SYSCALL_BASE && nr < OLUX_SYSCALL_BASE + NR_OLUX_SYSCALLS)
    fn = olux_table[nr - OLUX_SYSCALL_BASE];
  long ret;
  if (!fn) {
    ret = -ENOSYS;
    if (syscall_trace) pr_info("%s[%d]: unimplemented syscall %ld\n", current->proc->comm, current->proc->pid, nr);
  } else {
    lock_kernel();
    ret = fn(regs->regs[0], regs->regs[1], regs->regs[2], regs->regs[3], regs->regs[4], regs->regs[5]);
    unlock_kernel();
  }
  if (syscall_trace)
    pr_info("%s[%d]: %s(%#llx, %#llx, %#llx) = %ld\n", current->proc->comm, current->proc->pid,
            syscall_name(nr), (unsigned long long)regs->orig_x0, (unsigned long long)regs->regs[1],
            (unsigned long long)regs->regs[2], ret);
  if (nr == 139) return; /* rt_sigreturn restored the full register frame */
  regs->regs[0] = ret;
}
