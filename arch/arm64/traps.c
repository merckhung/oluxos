/*
 * Synchronous exception handling for EL0 and EL1, syscall entry and the
 * return-to-user work loop.
 */
#include <asm/ptrace.h>
#include <olux/kernel.h>
#include <olux/mm.h>
#include <olux/process.h>
#include <olux/sched.h>
#include <olux/syscall.h>
#include <olux/uaccess.h>
#include <olux/vm.h>

void el0_sync_handler(struct pt_regs *regs);
void el1_sync_handler(struct pt_regs *regs);
void bad_mode(struct pt_regs *regs, int reason, unsigned long esr);
void serror_handler(struct pt_regs *regs, unsigned long esr);
void prepare_exit_to_user(struct pt_regs *regs);

static const char *const ec_names[64] = {
    [ESR_EC_UNKNOWN] = "unknown/undefined instruction", [ESR_EC_FP_ASIMD] = "FP/SIMD access",
    [ESR_EC_ILL_STATE] = "illegal execution state", [ESR_EC_SVC64] = "SVC",
    [ESR_EC_SYS64] = "MSR/MRS", [ESR_EC_IABT_LOW] = "instruction abort (EL0)",
    [ESR_EC_IABT_CUR] = "instruction abort (EL1)", [ESR_EC_PC_ALIGN] = "PC alignment",
    [ESR_EC_DABT_LOW] = "data abort (EL0)", [ESR_EC_DABT_CUR] = "data abort (EL1)",
    [ESR_EC_SP_ALIGN] = "SP alignment", [ESR_EC_FP_EXC64] = "FP exception",
    [ESR_EC_SERROR] = "SError", [ESR_EC_BRK64] = "BRK",
};

static const char *ec_name(unsigned ec) { return ec_names[ec & 63] ? ec_names[ec & 63] : "exception"; }

/* Fault status: translation fault (page not present) vs permission etc. */
static bool is_translation_or_perm(unsigned long esr) {
  unsigned fsc = esr & ESR_ELx_FSC_MASK;
  return (fsc >= 0x04 && fsc <= 0x07) /* translation */ || (fsc >= 0x0c && fsc <= 0x0f) /* permission */ ||
         (fsc >= 0x08 && fsc <= 0x0b) /* access flag */;
}

static void user_fault(struct pt_regs *regs, unsigned long esr, u64 far, bool instr) {
  struct process *p = current->proc;
  unsigned fsc = esr & ESR_ELx_FSC_MASK;
  if (fsc == 0x21) { /* alignment fault */
    force_sig_fault(SIGBUS, BUS_ADRALN, far);
    return;
  }
  if (!is_translation_or_perm(esr)) {
    force_sig_fault(SIGBUS, 3 /* BUS_OBJERR */, far);
    return;
  }
  bool write = !instr && (esr & ESR_ELx_WNR) && !(esr & (1UL << 8)) /* not cache maint */;
  lock_kernel();
  int r = handle_mm_fault(p->mm, far, write, instr);
  if (r == 0) p->min_flt++;
  unlock_kernel();
  if (r == 0) return;
  if (r == -ENOMEM) {
    pr_err("out of memory: killing %s (pid %d)\n", p->comm, p->pid);
    force_sig_fault(SIGKILL, SI_KERNEL, far);
    return;
  }
  if (console_loglevel >= LOGLEVEL_INFO)
    pr_info("%s[%d]: segfault at %#llx pc %#llx sp %#llx (%s)\n", p->comm, p->pid, (unsigned long long)far,
            (unsigned long long)regs->pc, (unsigned long long)regs->sp, write ? "write" : instr ? "exec" : "read");
  force_sig_fault(SIGSEGV, r == -EACCES ? SEGV_ACCERR : SEGV_MAPERR, far);
}

void el0_sync_handler(struct pt_regs *regs) {
  unsigned long esr = read_sysreg(esr_el1);
  u64 far = read_sysreg(far_el1);
  unsigned ec = esr >> ESR_EC_SHIFT;
  local_irq_enable();
  switch (ec) {
    case ESR_EC_SVC64:
      do_syscall(regs);
      return;
    case ESR_EC_DABT_LOW:
      user_fault(regs, esr, far, false);
      return;
    case ESR_EC_IABT_LOW:
      user_fault(regs, esr, far, true);
      return;
    case ESR_EC_PC_ALIGN:
    case ESR_EC_SP_ALIGN:
      force_sig_fault(SIGBUS, BUS_ADRALN, ec == ESR_EC_PC_ALIGN ? regs->pc : regs->sp);
      return;
    case ESR_EC_FP_EXC64:
      force_sig_fault(SIGFPE, 0, regs->pc);
      return;
    case ESR_EC_BRK64:
    case ESR_EC_BREAKPT_LOW:
    case ESR_EC_SOFTSTP_LOW:
    case ESR_EC_WATCHPT_LOW:
      force_sig_fault(SIGTRAP, TRAP_BRKPT, regs->pc);
      return;
    case ESR_EC_UNKNOWN:
    case ESR_EC_ILL_STATE:
    case ESR_EC_SYS64:
    case ESR_EC_FP_ASIMD:
    default:
      if (current->proc && console_loglevel >= LOGLEVEL_INFO)
        pr_info("%s[%d]: %s at pc %#llx (ESR %#lx)\n", current->proc->comm, current->proc->pid, ec_name(ec),
                (unsigned long long)regs->pc, esr);
      force_sig_fault(SIGILL, ILL_ILLOPC, regs->pc);
      return;
  }
}

void el1_sync_handler(struct pt_regs *regs) {
  unsigned long esr = read_sysreg(esr_el1);
  u64 far = read_sysreg(far_el1);
  unsigned ec = esr >> ESR_EC_SHIFT;
  if (ec == ESR_EC_DABT_CUR && in_uaccess(regs->pc)) {
    /* fault on a user address inside copy_{to,from}_user */
    if (far < USER_VA_END && current->proc && current->proc->mm && is_translation_or_perm(esr)) {
      bool write = (esr & ESR_ELx_WNR) != 0;
      bool irqs_were_on = !(regs->pstate & PSR_I);
      if (irqs_were_on) local_irq_enable();
      lock_kernel();
      int r = handle_mm_fault(current->proc->mm, far, write, false);
      unlock_kernel();
      local_irq_disable();
      if (r == 0) return; /* retry the access */
    }
    if (fixup_exception(&regs->pc)) return;
  }
  if (ec == ESR_EC_DABT_CUR && far >= VMALLOC_START && far < VMALLOC_END) {
    /* guard page below a kernel stack? */
    u64 sp = regs->sp;
    if (far < sp + PAGE_SIZE && far + 2 * PAGE_SIZE > sp) pr_emerg("kernel stack overflow (sp %#llx)\n", (unsigned long long)sp);
  }
  pr_emerg("Unable to handle kernel %s at virtual address %#llx\n", ec_name(ec), (unsigned long long)far);
  die("Oops", regs, esr);
}

void bad_mode(struct pt_regs *regs, int reason, unsigned long esr) {
  static const char *const reasons[] = {"sync", "irq", "fiq", "serror", "aarch32"};
  if (user_mode(regs) && current->proc) {
    force_sig_fault(SIGILL, ILL_ILLOPC, regs->pc);
    prepare_exit_to_user(regs);
    return;
  }
  pr_emerg("Bad mode in %s handler detected (ESR %#lx)\n", reasons[reason], esr);
  die("bad mode", regs, esr);
}

void serror_handler(struct pt_regs *regs, unsigned long esr) {
  if (user_mode(regs) && current->proc) {
    pr_err("SError in user process %s[%d] (ESR %#lx)\n", current->proc->comm, current->proc->pid, esr);
    force_sig_fault(SIGBUS, 0, regs->pc);
    return;
  }
  die("asynchronous SError", regs, esr);
}

/* Runs with IRQs disabled on every return to EL0; returns with them off. */
void prepare_exit_to_user(struct pt_regs *regs) {
  struct thread *t = current;
  for (;;) {
    if (this_cpu()->need_resched) {
      local_irq_enable();
      schedule();
      local_irq_disable();
      continue;
    }
    if (signal_pending(t) || t->restore_sigmask) {
      local_irq_enable();
      lock_kernel();
      do_signal(regs);
      unlock_kernel();
      local_irq_disable();
      if (signal_pending(t) && !t->proc->group_stop) {
        /* only unhandled blocked-then-unblocked races loop here */
        sigset_t pend = t->sig_pending | t->proc->shared_pending;
        if (!(pend & ~t->sig_blocked) && !t->proc->exiting) break;
        continue;
      }
      continue;
    }
    break;
  }
}
