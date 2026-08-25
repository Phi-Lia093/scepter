/* ============================================================================
 * Signal Implementation
 *
 * A small, self-contained signal subsystem:
 *   - task_struct carries pending/blocked masks and a per-signal action table
 *   - sys_signal()/sys_sigaction() install dispositions (SIG_DFL/SIG_IGN or a
 *     user function), including the POSIX sa_mask / SA_* flags
 *   - sys_kill()/send_signal() set the pending bit on a task (or a whole
 *     process group); SIGCONT resumes a stopped task
 *   - do_signal() is called at syscall-return and interrupt-return to user
 *     mode; it delivers the lowest pending (unblocked) signal:
 *       * default action  -> terminate the task, STOP it (SIGSTOP/TSTP/...),
 *                            or ignore it (SIGCHLD, SIGCONT, ...)
 *       * user handler    -> run it on the user stack with a tiny trampoline
 *                            that returns via the sigreturn() syscall
 *   - wait4() reports stops (WUNTRACED) and continues (WCONTINUED)
 *
 * Signal delivery touches only the current task's kernel-stack register
 * frame (the same per-process-stack principle as the wait-queue fix): no
 * global state, so blocked/timer contexts are safe.
 * ============================================================================ */

#include "kernel/signal.h"
#include "kernel/sched.h"
#include "kernel/process.h"
#include "lib/printk.h"
#include "errno.h"
#include "lib/string.h"

/* ============================================================================
 * Default action tables
 * ============================================================================ */

/* Signals whose default action is to terminate the task. */
static int sig_default_terminates(int sig)
{
    switch (sig) {
        case SIGHUP:
        case SIGINT:
        case SIGQUIT:
        case SIGILL:
        case SIGTRAP:
        case SIGABRT:
        case SIGBUS:
        case SIGFPE:
        case SIGKILL:
        case SIGUSR1:
        case SIGSEGV:
        case SIGUSR2:
        case SIGPIPE:
        case SIGALRM:
        case SIGTERM:
        case SIGXCPU:
        case SIGXFSZ:
        case SIGVTALRM:
        case SIGPROF:
        case SIGIO:
        case SIGPWR:
        case SIGSYS:
            return 1;
        default:
            return 0;   /* others default to ignore/stop/continue */
    }
}

/* Signals whose default action is to stop the task (until SIGCONT). */
static int sig_default_stops(int sig)
{
    return sig == SIGSTOP || sig == SIGTSTP ||
           sig == SIGTTIN || sig == SIGTTOU;
}

/* ============================================================================
 * Action installation
 * ============================================================================ */

/**
 * sys_signal - Install a handler for a signal (or SIG_DFL/SIG_IGN).
 */
int sys_signal(int signum, uint32_t handler)
{
    if (signum < 1 || signum >= NSIG)
        return -EINVAL;
    /* SIGKILL and SIGSTOP can never be caught or ignored */
    if (signum == SIGKILL || signum == SIGSTOP)
        return -EINVAL;

    task_struct_t *task = current;
    uint32_t old = task->sig_handlers[signum];
    task->sig_handlers[signum] = handler;
    task->sig_hmask[signum]    = 0;
    task->sig_hflags[signum]   = 0;
    return (int)old;
}

/**
 * sys_sigaction - Install a signal handler with POSIX semantics.
 */
int sys_sigaction(int signum, sigaction_t *user_new, sigaction_t *user_old)
{
    if (signum < 1 || signum >= NSIG)
        return -EINVAL;
    if (signum == SIGKILL || signum == SIGSTOP)
        return -EINVAL;

    task_struct_t *task = current;

    /* Write the old action first (Linux semantics: old is filled even if
     * the new action turns out to be invalid). */
    if (user_old) {
        if (!valid_user_pointer(user_old, sizeof(sigaction_t)))
            return -EFAULT;
        sigaction_t old;
        old.sa_handler  = task->sig_handlers[signum];
        old.sa_mask     = task->sig_hmask[signum];
        old.sa_flags    = task->sig_hflags[signum];
        old.sa_restorer = 0;
        if (copy_to_user(user_old, &old, sizeof(old)) < 0)
            return -EFAULT;
    }

    if (user_new) {
        if (!valid_user_pointer(user_new, sizeof(sigaction_t)))
            return -EFAULT;
        sigaction_t n;
        if (copy_from_user(&n, user_new, sizeof(n)) < 0)
            return -EFAULT;

        /* SIG_DFL/SIG_IGN or a valid user-space handler address. */
        task->sig_handlers[signum] = n.sa_handler;
        task->sig_hmask[signum]    = n.sa_mask;
        task->sig_hflags[signum]   = n.sa_flags;
    }
    return 0;
}

/**
 * sys_sigprocmask - Examine / change the blocked signal mask.
 */
int sys_sigprocmask(int how, sigset_t *user_new, sigset_t *user_old)
{
    task_struct_t *task = current;

    if (user_old) {
        if (!valid_user_pointer(user_old, sizeof(sigset_t)))
            return -EFAULT;
        if (copy_to_user(user_old, &task->blocked, sizeof(sigset_t)) < 0)
            return -EFAULT;
    }

    if (!user_new)
        return 0;   /* query only */

    if (!valid_user_pointer(user_new, sizeof(sigset_t)))
        return -EFAULT;

    sigset_t new;
    if (copy_from_user(&new, user_new, sizeof(new)) < 0)
        return -EFAULT;

    /* SIGKILL and SIGSTOP can never be blocked. */
    new &= ~((1u << SIGKILL) | (1u << SIGSTOP));

    switch (how) {
        case SIG_BLOCK:
            task->blocked |= new;
            break;
        case SIG_UNBLOCK:
            task->blocked &= ~new;
            break;
        case SIG_SETMASK:
            task->blocked = new;
            break;
        default:
            return -EINVAL;
    }
    return 0;
}

/**
 * sys_sigpending - Examine pending (not-yet-delivered) signals.
 * POSIX: returns the set of signals that are pending AND blocked from
 * delivery (i.e. waiting for the block to be lifted).
 */
int sys_sigpending(sigset_t *user_set)
{
    if (!user_set || !valid_user_pointer(user_set, sizeof(sigset_t)))
        return -EFAULT;

    sigset_t set = current->pending & current->blocked;
    if (copy_to_user(user_set, &set, sizeof(set)) < 0)
        return -EFAULT;
    return 0;
}

/**
 * sys_sigsuspend - Temporarily replace the signal mask and wait.
 * Returns -EINTR once a signal wakes us (the signal is delivered by
 * do_signal at the syscall return).
 */
int sys_sigsuspend(sigset_t *user_mask)
{
    if (!user_mask || !valid_user_pointer(user_mask, sizeof(sigset_t)))
        return -EFAULT;

    sigset_t mask;
    if (copy_from_user(&mask, user_mask, sizeof(mask)) < 0)
        return -EFAULT;

    task_struct_t *task = current;
    uint32_t old = task->blocked;
    task->blocked = mask & ~((1u << SIGKILL) | (1u << SIGSTOP));

    sleep_on(&task->wait);
    task->blocked = old;
    return -EINTR;
}

/**
 * send_signal - Set the pending bit on a task (no delivery here).
 * Safe from interrupt context: only touches task fields + wake_up().
 */
int send_signal(uint32_t pid, int signum)
{
    if (signum < 1 || signum >= NSIG)
        return -1;

    task_struct_t *task = find_task_by_pid(pid);
    if (!task)
        return -1;

    task->pending |= (1u << signum);

    /* SIGCONT: resume a stopped task and wake a blocked one. */
    if (signum == SIGCONT) {
        if (task->state == TASK_STOPPED) {
            task->state = TASK_READY;
            task->stop_sig = 0;
            task->stop_reported = 0;
            task->continued = 1;
            task_struct_t *parent = find_task_by_pid(task->ppid);
            if (parent && parent != task)
                wake_up(&parent->wait);
        } else if (task->state == TASK_BLOCKED) {
            wake_up(&task->wait);
        }
        return 0;
    }

    /* A stopped task must be woken so it can handle a terminating signal. */
    if (task->state == TASK_STOPPED && sig_default_terminates(signum)) {
        task->state = TASK_READY;
        task->stop_sig = 0;
        task->stop_reported = 0;
    }

    /* Wake a blocked task so it can notice the pending signal
     * (read()/nanosleep() return -EINTR; wait() re-scans). */
    if (task->state == TASK_BLOCKED)
        wake_up(&task->wait);

    return 0;
}

/**
 * send_signal_group - Send a signal to every member of a process group.
 */
int send_signal_group(int pgid, int signum)
{
    int matched = 0;
    list_head_t *pos;
    list_head_t *tl = task_list_head();
    list_for_each(pos, tl) {
        task_struct_t *t = list_entry(pos, task_struct_t, task_list);
        if (t->pid == 0)
            continue;
        if ((int)t->pgid != pgid)
            continue;
        send_signal(t->pid, signum);
        matched = 1;
    }
    return matched;
}

/**
 * send_signal_all - Send a signal to every user process except PID 0.
 */
int send_signal_all(int signum)
{
    int matched = 0;
    list_head_t *pos;
    list_head_t *tl = task_list_head();
    list_for_each(pos, tl) {
        task_struct_t *t = list_entry(pos, task_struct_t, task_list);
        if (t->pid == 0)
            continue;
        send_signal(t->pid, signum);
        matched = 1;
    }
    return matched;
}

/**
 * sys_kill - kill() syscall: send a signal to a process or process group.
 */
int sys_kill(int pid, int signum)
{
    if (signum < 1 || signum >= NSIG)
        return -EINVAL;

    if (pid > 0)
        return send_signal((uint32_t)pid, signum) < 0 ? -ESRCH : 0;

    if (pid == 0)
        return send_signal_group((int)current->pgid, signum) ? 0 : -ESRCH;

    if (pid == -1)
        return send_signal_all(signum) ? 0 : -ESRCH;

    /* pid < -1: process group -pid */
    return send_signal_group(-pid, signum) ? 0 : -ESRCH;
}

/**
 * sys_sigreturn - Restore the user context saved at signal delivery.
 */
int sys_sigreturn(registers_t *regs)
{
    task_struct_t *task = current;
    if (!task->sig_active)
        return -1;

    task->sig_active = 0;
    task->blocked    = task->sig_saved_blocked;
    regs->eip        = task->sig_saved_eip;
    regs->user_esp   = task->sig_saved_esp;
    regs->eflags     = task->sig_saved_eflags;
    regs->eax        = 0;
    return 0;
}

/**
 * sys_nice - Adjust the calling process's scheduling priority.
 * Linux semantics: lower nice value = higher priority.
 * @param inc Increment (negative = higher priority; clamped to [-20,19])
 * @return The new nice value
 */
int sys_nice(int inc)
{
    task_struct_t *task = current;
    int prio = task->priority + inc;
    if (prio < -20)
        prio = -20;
    if (prio > 19)
        prio = 19;
    task->priority = prio;
    return prio;
}

/**
 * do_signal - Deliver pending signals to the current task.
 *
 * Called from isr128 (syscall return) and from the IRQ stubs (return to
 * user mode).  regs points at a frame whose eip/cs/eflags/user_esp fields
 * live at offsets 52..68 -- the same layout as registers_t's IRET frame.
 *
 * A stop signal (default action) switches the task to TASK_STOPPED and
 * calls schedule(); the task resumes here (via SIGCONT) and re-scans the
 * pending mask, so a fatal signal delivered while stopped is then handled.
 */
void do_signal(registers_t *regs)
{
    task_struct_t *task = current;
    if (!task || task->pid == 0)
        return;
    if (task->pending == 0)
        return;

    for (int sig = 1; sig < NSIG; sig++) {
        uint32_t mask = (1u << sig);
        if (!(task->pending & mask))
            continue;
        if (task->blocked & mask)
            continue;

        /* Consume the signal now. */
        task->pending &= ~mask;

        uint32_t handler = task->sig_handlers[sig];

        /* Ignored? */
        if (handler == SIG_IGN)
            continue;

        /* Default action? */
        if (handler == SIG_DFL) {
            if (sig_default_terminates(sig)) {
                printk("[SIG] pid %d (%s): killed by signal %d\n",
                       task->pid, task->name, sig);
                do_exit(128 + sig);         /* never returns */
            }
            if (sig_default_stops(sig)) {
                /* Stop the task.  It resumes (here) when SIGCONT arrives. */
                task->state = TASK_STOPPED;
                task->stop_sig = sig;
                task->stop_reported = 0;
                task->continued = 0;

                /* Wake a parent waiting in waitpid(WUNTRACED). */
                task_struct_t *parent = find_task_by_pid(task->ppid);
                if (parent && parent != task)
                    wake_up(&parent->wait);

                schedule();                 /* blocked until SIGCONT */
                continue;                   /* re-scan pending (e.g. SIGKILL) */
            }
            continue;                       /* default ignore: SIGCHLD, ... */
        }

        /* Catchable handler.  If another handler is already running,
         * re-queue this signal and deliver it after sigreturn. */
        if (task->sig_active) {
            task->pending |= mask;
            continue;
        }

        /* SA_RESETHAND: reset to default before running the handler. */
        if (task->sig_hflags[sig] & SA_RESETHAND)
            task->sig_handlers[sig] = SIG_DFL;

        extern int copy_to_user(void *user_dst, const void *kernel_src, size_t n);

        /* Write the sigreturn trampoline into the top of the stack page:
         *   B8 <imm32>  CD 80     (mov $SYS_SIGRETURN,%eax ; int $0x80) */
        uint8_t tramp[7] = {
            0xB8,
            (uint8_t)(SYS_SIGRETURN & 0xFF),
            (uint8_t)((SYS_SIGRETURN >> 8) & 0xFF),
            (uint8_t)((SYS_SIGRETURN >> 16) & 0xFF),
            (uint8_t)((SYS_SIGRETURN >> 24) & 0xFF),
            0xCD, 0x80
        };
        if (copy_to_user((void *)SIGNAL_TRAMPOLINE_VA, tramp, sizeof(tramp)) < 0)
            continue;                   /* can't deliver right now */

        /* Save the interrupted user context in the task struct.  Block the
         * signal itself (unless SA_NODEFER) plus the action's sa_mask while
         * the handler runs; sys_sigreturn restores the old mask. */
        uint32_t old_blocked = task->blocked;
        uint32_t add = task->sig_hmask[sig];
        if (!(task->sig_hflags[sig] & SA_NODEFER))
            add |= mask;
        task->blocked |= add;

        task->sig_saved_eip     = regs->eip;
        task->sig_saved_esp     = regs->user_esp;
        task->sig_saved_eflags  = regs->eflags;
        task->sig_saved_blocked = old_blocked;
        task->sig_active        = 1;
        task->sig_delivered     = sig;

        /* Build the handler call frame on the user stack:
         *   [esp]   return address = trampoline
         *   [esp+4] arg = signum
         * The handler is a C function void f(int); its ret pops the
         * trampoline address, which then syscalls sigreturn. */
        uint32_t new_esp = regs->user_esp - 8;
        uint32_t frame[2] = { SIGNAL_TRAMPOLINE_VA, (uint32_t)sig };
        if (copy_to_user((void *)new_esp, frame, sizeof(frame)) < 0) {
            task->sig_active = 0;
            task->blocked = old_blocked;
            continue;
        }

        regs->user_esp = new_esp;
        regs->eip      = handler;
        regs->eax      = 0;
        return;
    }
}


