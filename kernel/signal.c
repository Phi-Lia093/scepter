/* ============================================================================
 * Signal Implementation
 *
 * A small, self-contained signal subsystem:
 *   - task_struct carries pending/blocked masks and a handler table
 *   - sys_signal() installs handlers (SIG_DFL/SIG_IGN or a user function)
 *   - sys_kill()/send_signal() set the pending bit on a task
 *   - do_signal() is called at syscall-return and interrupt-return to user
 *     mode; it delivers the lowest pending (unblocked) signal:
 *       * default action  -> terminate the task (unless ignored, e.g. SIGCHLD)
 *       * user handler    -> run it on the user stack with a tiny trampoline
 *                            that returns via the sigreturn() syscall
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

/* Signals whose default action is to terminate the task. */
static int sig_default_terminates(int sig)
{
    switch (sig) {
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
            return 1;
        default:
            return 0;   /* others default to ignore (SIGCHLD, SIGCONT, ...) */
    }
}

/**
 * sys_signal - Install a signal handler.
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
    return (int)old;
}

/**
 * send_signal - Mark a signal pending on a task.
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

    /* If the target is blocked in wait(), wake it so it can deliver the
     * signal promptly.  (Tasks blocked in read()/nanosleep() are not woken;
     * they see the signal when their wait completes.) */
    if (task->state == TASK_BLOCKED)
        wake_up(&task->wait);

    return 0;
}

/**
 * sys_kill - kill() syscall: send a signal to a process.
 */
int sys_kill(int pid, int signum)
{
    if (signum < 1 || signum >= NSIG)
        return -EINVAL;
    if (send_signal((uint32_t)pid, signum) < 0)
        return -ESRCH;
    return 0;
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
    regs->eip     = task->sig_saved_eip;
    regs->user_esp = task->sig_saved_esp;
    regs->eflags  = task->sig_saved_eflags;
    regs->eax     = 0;
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
            if (!sig_default_terminates(sig))
                continue;               /* e.g. SIGCHLD: ignore */
            printk("[SIG] pid %d (%s): killed by signal %d\n",
                   task->pid, task->name, sig);
            do_exit(128 + sig);         /* never returns */
        }

        /* Catchable handler.  If another handler is already running,
         * re-queue this signal and deliver it after sigreturn. */
        if (task->sig_active) {
            task->pending |= mask;
            continue;
        }

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

        /* Save the interrupted user context in the task struct. */
        task->sig_saved_eip    = regs->eip;
        task->sig_saved_esp    = regs->user_esp;
        task->sig_saved_eflags = regs->eflags;
        task->sig_active       = 1;
        task->sig_delivered    = sig;

        /* Build the handler call frame on the user stack:
         *   [esp]   return address = trampoline
         *   [esp+4] arg = signum
         * The handler is a C function void f(int); its ret pops the
         * trampoline address, which then syscalls sigreturn. */
        uint32_t new_esp = regs->user_esp - 8;
        uint32_t frame[2] = { SIGNAL_TRAMPOLINE_VA, (uint32_t)sig };
        if (copy_to_user((void *)new_esp, frame, sizeof(frame)) < 0) {
            task->sig_active = 0;
            continue;
        }

        regs->user_esp = new_esp;
        regs->eip      = handler;
        regs->eax      = 0;
        return;
    }
}


