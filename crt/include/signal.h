#ifndef _SIGNAL_H
#define _SIGNAL_H

#include <sys/types.h>

/* ============================================================================
 * Signal numbers (must match kernel include/kernel/signal.h)
 * ============================================================================ */

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

/* Dispositions */
typedef void (*sighandler_t)(int);
#define SIG_DFL ((sighandler_t)0)
#define SIG_IGN ((sighandler_t)1)
#define SIG_ERR ((sighandler_t)-1)

/* sigprocmask() how values */
#define SIG_BLOCK   0
#define SIG_UNBLOCK 1
#define SIG_SETMASK 2

/* sigaction() flags (subset of Linux values) */
#define SA_NOCLDSTOP 0x00000001
#define SA_RESTART   0x10000000
#define SA_NODEFER   0x40000000
#define SA_RESETHAND 0x80000000

/* A signal mask (32-bit bitmask; bit i = signal i).  The kernel ABI is a
 * plain uint32_t; field layout of struct sigaction MUST match the kernel
 * sigaction_t in include/kernel/signal.h (16 bytes). */
typedef uint32_t sigset_t;

struct sigaction {
    sighandler_t sa_handler;
    sigset_t     sa_mask;
    int          sa_flags;
    void         (*sa_restorer)(void);
};

sighandler_t signal(int signum, sighandler_t handler);
int kill(pid_t pid, int sig);
int killpg(int pgrp, int sig);
int raise(int sig);

int sigaction(int signum, const struct sigaction *act,
              struct sigaction *oldact);
int sigprocmask(int how, const sigset_t *set, sigset_t *oldset);
int sigpending(sigset_t *set);
int sigsuspend(const sigset_t *mask);

/* Simple sigset manipulation (static inline, no syscalls) */
static inline int sigemptyset(sigset_t *set)      { *set = 0; return 0; }
static inline int sigfillset(sigset_t *set)       { *set = ~(sigset_t)0; return 0; }
static inline int sigaddset(sigset_t *set, int s) { *set |= (sigset_t)1 << s; return 0; }
static inline int sigdelset(sigset_t *set, int s) { *set &= ~((sigset_t)1 << s); return 0; }
static inline int sigismember(const sigset_t *set, int s) {
    return !!(*set & ((sigset_t)1 << s));
}

#endif /* _SIGNAL_H */
