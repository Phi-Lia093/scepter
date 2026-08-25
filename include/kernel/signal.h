#ifndef KERNEL_SIGNAL_H
#define KERNEL_SIGNAL_H

#include <stdint.h>
#include "kernel/syscall.h"

/* =========================================================================
 * Signal numbers (standard Linux values)
 * ========================================================================= */

#define SIGHUP    1
#define SIGINT    2
#define SIGQUIT   3
#define SIGILL    4
#define SIGTRAP   5
#define SIGABRT   6
#define SIGBUS    7
#define SIGFPE    8
#define SIGKILL   9
#define SIGUSR1   10
#define SIGSEGV   11
#define SIGUSR2   12
#define SIGPIPE   13
#define SIGALRM   14
#define SIGTERM   15
#define SIGSTKFLT 16
#define SIGCHLD   17
#define SIGCONT   18
#define SIGSTOP   19
#define SIGTSTP   20
#define SIGTTIN   21
#define SIGTTOU   22
#define SIGURG    23
#define SIGXCPU   24
#define SIGXFSZ   25
#define SIGVTALRM 26
#define SIGPROF   27
#define SIGWINCH  28
#define SIGIO     29
#define SIGPWR    30
#define SIGSYS    31

#define NSIG 32

/* Disposition values (user handler slot in task_struct) */
#define SIG_DFL 0
#define SIG_IGN 1

/* sigaction() flags (subset of Linux values) */
#define SA_NOCLDSTOP 0x00000001   /* do not generate SIGCHLD on child stop  */
#define SA_RESTART   0x10000000   /* restart interrupted syscalls (accepted,
                                     not yet fully honoured)                */
#define SA_NODEFER   0x40000000   /* do not block the signal in its handler */
#define SA_RESETHAND 0x80000000   /* reset handler to SIG_DFL on delivery   */

/* sigprocmask() how values */
#define SIG_BLOCK   0
#define SIG_UNBLOCK 1
#define SIG_SETMASK 2

/* A signal mask (sigset_t on the user ABI is a plain 32-bit bitmask). */
typedef uint32_t sigset_t;

/* Kernel-ABI struct sigaction.  Field layout MUST match the user struct
 * in crt/include/signal.h (the kernel copies raw bytes to/from userspace):
 *   [0]  handler   [4]  mask   [8]  flags   [12]  restorer   -> 16 bytes */
typedef struct {
    uint32_t sa_handler;
    uint32_t sa_mask;
    int      sa_flags;
    uint32_t sa_restorer;
} sigaction_t;

/* The sigreturn trampoline is written by the kernel into the top of the
 * user stack page (always mapped, above the initial ESP) just before a
 * catchable handler is entered.  It executes:
 *     mov  $SYS_SIGRETURN, %eax
 *     int  $0x80
 * which is 7 bytes:  B8 <imm32>  CD 80
 */
#define SIGNAL_TRAMPOLINE_VA 0xBFFFFFE0U

/* =========================================================================
 * Signal API
 * ========================================================================= */

/**
 * sys_signal - Install a handler for a signal (or SIG_DFL/SIG_IGN).
 * @param signum  Signal number (1..31, SIGKILL/SIGSTOP are not catchable)
 * @param handler User handler address, SIG_DFL(0) or SIG_IGN(1)
 * @return Previous handler value, or -1 on error
 */
int sys_signal(int signum, uint32_t handler);

/**
 * sys_sigaction - Install a signal handler with POSIX semantics.
 * @param signum  Signal number
 * @param user_new  User pointer to new struct sigaction (may be NULL)
 * @param user_old  User pointer to receive the previous action (may be NULL)
 * @return 0 on success, -errno on error
 */
int sys_sigaction(int signum, sigaction_t *user_new, sigaction_t *user_old);

/**
 * sys_sigprocmask - Examine / change the blocked signal mask.
 * @param how       SIG_BLOCK / SIG_UNBLOCK / SIG_SETMASK
 * @param user_new  User pointer to new mask (may be NULL)
 * @param user_old  User pointer to receive the old mask (may be NULL)
 * @return 0 on success, -errno on error
 */
int sys_sigprocmask(int how, sigset_t *user_new, sigset_t *user_old);

/**
 * sys_sigpending - Examine pending (not-yet-delivered) signals.
 * @param user_set  User pointer to receive the set of pending+unblocked signals
 * @return 0 on success, -errno on error
 */
int sys_sigpending(sigset_t *user_set);

/**
 * sys_sigsuspend - Temporarily replace the signal mask and wait for a signal.
 * @param user_mask  User pointer to the temporary mask
 * @return Never returns 0; returns -EINTR after a signal is caught
 */
int sys_sigsuspend(sigset_t *user_mask);

/**
 * sys_kill - Send a signal to a process.
 * @param pid    >0: PID; 0: caller's process group; -1: all user processes;
 *              < -1: process group -pid
 * @param signum Signal number
 * @return 0 on success, -errno on error
 */
int sys_kill(int pid, int signum);

/**
 * sys_sigreturn - Return from a signal handler (called via trampoline).
 * Restores the user context saved when the signal was delivered.
 * @param regs The int 0x80 register frame (modified in place)
 * @return 0 (never actually returned to user; regs are rewritten)
 */
int sys_sigreturn(registers_t *regs);

/**
 * sys_nice - Adjust the calling process's scheduling priority.
 * @param inc Increment (negative = higher priority; clamped to [0,20])
 * @return The new priority value
 */
int sys_nice(int inc);

/**
 * do_signal - Deliver pending signals to the current task.
 * Called at syscall return and at interrupt return to user mode.
 * May modify regs (catchable handler) or kill the task (default action);
 * in the latter case it never returns.
 * @param regs Interrupt/syscall register frame (eip/esp/eflags/eax slots)
 */
void do_signal(registers_t *regs);

/**
 * send_signal - Set the pending bit on a task (no delivery here).
 * Safe to call from interrupt context (IF=0).
 * Handles the special cases: SIGCONT resumes a stopped task; a stopped
 * task is woken to handle terminating signals.
 * @param pid    Target PID
 * @param signum Signal number
 * @return 0 on success, -1 if no such process or bad signal
 */
int send_signal(uint32_t pid, int signum);

/**
 * send_signal_group - Send a signal to every member of a process group
 * (excluding the kernel task PID 0).
 * @param pgid    Process group id
 * @param signum  Signal number
 * @return 1 if at least one process matched, 0 otherwise
 */
int send_signal_group(int pgid, int signum);

/**
 * send_signal_all - Send a signal to every user process except PID 0.
 * @param signum  Signal number
 * @return 1 if at least one process matched, 0 otherwise
 */
int send_signal_all(int signum);

#endif /* KERNEL_SIGNAL_H */
