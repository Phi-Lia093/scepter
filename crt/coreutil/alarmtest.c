/* ============================================================================
 * alarmtest - minimal repro for signal + sigreturn stack corruption.
 * ============================================================================ */
#include <stdio.h>
#include <signal.h>
#include <sys/time.h>
#include <unistd.h>

static volatile int fired = 0;
static volatile unsigned long ebp_at_loop = 0;
static void on_alarm(int s) { (void)s; fired++; }

int main(void)
{
    __asm__ volatile("mov %%ebp, %0" : "=r"(ebp_at_loop));
    printf("ebp at start=%lx\n", ebp_at_loop);
    signal(SIGALRM, on_alarm);

    struct itimerval itv;
    itv.it_value.tv_sec = 0; itv.it_value.tv_usec = 150000;   /* 15 ticks, not a schedule tick */
    itv.it_interval.tv_sec = 0; itv.it_interval.tv_usec = 0;
    setitimer(ITIMER_REAL, &itv, NULL);
    printf("alarm set\n");

    int spins = 0;
    while (!fired && spins++ < 10000000) { }
    printf("fired=%d\n", fired);

    /* pause() is the POSIX-correct wait; a busy loop would rely on IRQ-
     * time signal delivery which this kernel defers to syscall return. */
    while (!fired)
        pause();
    printf("paused-and-fired=%d\n", fired);

    struct itimerval old;
    if (getitimer(ITIMER_REAL, &old) == 0)
        printf("getitimer ok: rem=%ld.%06ld\n", old.it_value.tv_sec, old.it_value.tv_usec);
    else
        printf("getitimer FAILED\n");

    setitimer(ITIMER_REAL, &(struct itimerval){0}, NULL);
    signal(SIGALRM, SIG_DFL);
    printf("done\n");
    return 0;
}
