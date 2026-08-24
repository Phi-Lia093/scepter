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
#define SIGCHLD   17
#define SIGCONT   18
#define SIGSTOP   19
#define SIGTSTP   20

#define NSIG 32

/* Disposition values (user handler slot in task_struct) */
#define SIG_DFL 0
#define SIG_IGN 1

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
 * sys_kill - Send a signal to a process.
 * @param pid    Target PID (must exist)
 * @param signum Signal number
 * @return 0 on success, -1 if no such process or bad signal
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
 * @param pid    Target PID
 * @param signum Signal number
 * @return 0 on success, -1 if no such process or bad signal
 */
int send_signal(uint32_t pid, int signum);

#endif /* KERNEL_SIGNAL_H */
