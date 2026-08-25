/* ============================================================================
 * sigtest - Phase A regression test: uid/gid, process groups/sessions,
 * signals (signal/sigaction/sigprocmask/sigpending/sigsuspend), and
 * stop/continue waitpid semantics (WUNTRACED / WCONTINUED / killpg).
 * ============================================================================ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <errno.h>

static volatile int sigusr1_caught = 0;
static volatile int sigusr2_caught = 0;

static void handler1(int sig) { (void)sig; sigusr1_caught = 1; }
static void handler2(int sig) { (void)sig; sigusr2_caught = 1; }

static int failures = 0;

static void check(int cond, const char *msg)
{
    if (cond)
        printf("PASS: %s\n", msg);
    else {
        printf("FAIL: %s\n", msg);
        failures++;
    }
}

int main(void)
{
    printf("sigtest: phase A regression tests\n");

    /* ---- credentials ---- */
    check(getuid() == 0 && geteuid() == 0, "getuid/geteuid == 0 (root)");
    check(getgid() == 0 && getegid() == 0, "getgid/getegid == 0");

    /* ---- sessions & process groups ---- */
    pid_t me = getpid();
    check(setsid() == me, "setsid() == getpid()");
    check(getsid(0) == me, "getsid(0) == getpid()");
    check(getpgrp() == me, "getpgrp() == getpid()");
    check(setpgid(0, 0) == 0, "setpgid(0,0)");
    check(getpgid(0) == me, "getpgid(0) == getpid()");

    /* ---- signal() handler ---- */
    check(signal(SIGUSR1, handler1) == SIG_DFL, "signal() returns SIG_DFL");
    raise(SIGUSR1);
    check(sigusr1_caught == 1, "signal(SIGUSR1) handler ran");

    /* ---- sigaction: install + old action ---- */
    struct sigaction sa, old;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handler2;
    sigemptyset(&sa.sa_mask);
    check(sigaction(SIGUSR2, &sa, &old) == 0, "sigaction(SIGUSR2)");
    check(old.sa_handler == SIG_DFL, "old action was SIG_DFL");
    raise(SIGUSR2);
    check(sigusr2_caught == 1, "sigaction handler ran");

    /* ---- sigprocmask + sigpending + deferred delivery ---- */
    sigset_t set, oldset, pend;
    sigemptyset(&set);
    sigaddset(&set, SIGUSR2);
    check(sigprocmask(SIG_BLOCK, &set, &oldset) == 0, "sigprocmask(SIG_BLOCK)");
    sigusr2_caught = 0;
    raise(SIGUSR2);   /* now pending but blocked */
    check(sigpending(&pend) == 0 && sigismember(&pend, SIGUSR2),
          "SIGUSR2 pending while blocked");
    check(sigprocmask(SIG_SETMASK, &oldset, NULL) == 0, "sigprocmask restore");
    getpid();          /* the syscall return delivers the now-unblocked signal */
    check(sigusr2_caught == 1, "deferred SIGUSR2 delivered after unblock");

    /* ---- fork + SIGSTOP + WUNTRACED / WCONTINUED / reap ---- */
    pid_t child = fork();
    if (child == 0) {
        raise(SIGSTOP);
        for (volatile int i = 0; i < 200000; i++)
            ;
        _exit(7);
    }

    int status = 0;
    pid_t r = waitpid(child, &status, WUNTRACED);
    check(r == child && WIFSTOPPED(status) && WSTOPSIG(status) == SIGSTOP,
          "waitpid(WUNTRACED) reports SIGSTOP stop");
    check(kill(child, SIGCONT) == 0, "kill(child, SIGCONT)");
    r = waitpid(child, &status, WCONTINUED);
    check(r == child && WIFCONTINUED(status), "waitpid(WCONTINUED) reports");
    r = waitpid(child, &status, 0);
    check(r == child && WIFEXITED(status) && WEXITSTATUS(status) == 7,
          "reaped child exit 7");

    /* ---- killpg + group signalling ---- */
    pid_t c2 = fork();
    if (c2 == 0) {
        setpgid(0, 0);
        for (;;)
            pause();
    }
    check(setpgid(c2, 0) == 0, "parent setpgid(child, 0)");
    check(getpgid(c2) == c2, "getpgid(child) == child pid");
    check(killpg(c2, SIGTERM) == 0, "killpg(child, SIGTERM)");
    r = waitpid(c2, &status, 0);
    check(r == c2 && WIFSIGNALED(status) && WTERMSIG(status) == SIGTERM,
          "child terminated by SIGTERM via killpg");

    if (failures == 0)
        printf("sigtest: ALL TESTS PASSED\n");
    else
        printf("sigtest: %d FAILURE(S)\n", failures);
    return failures;
}
