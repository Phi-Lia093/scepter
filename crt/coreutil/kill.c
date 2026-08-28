/* kill - send a signal to a process */
#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "unistd.h"
#include "signal.h"

static const char *sig_name(int sig)
{
    switch (sig) {
    case SIGHUP: return "HUP";
    case SIGINT: return "INT";
    case SIGQUIT: return "QUIT";
    case SIGILL: return "ILL";
    case SIGABRT: return "ABRT";
    case SIGFPE: return "FPE";
    case SIGKILL: return "KILL";
    case SIGSEGV: return "SEGV";
    case SIGPIPE: return "PIPE";
    case SIGALRM: return "ALRM";
    case SIGTERM: return "TERM";
    case SIGCHLD: return "CHLD";
    case SIGCONT: return "CONT";
    case SIGSTOP: return "STOP";
    case SIGTSTP: return "TSTP";
    case SIGUSR1: return "USR1";
    case SIGUSR2: return "USR2";
    default: return "?";
    }
}

int main(int argc, char *argv[])
{
    int sig = SIGTERM;

    if (argc < 2) {
        fprintf(stderr, "usage: kill [-s SIGNAL|-SIGNAL] PID...\n"
                        "       kill -l\n");
        return 1;
    }

    int a = 1;
    if (strcmp(argv[1], "-l") == 0) {
        printf(" 1 HUP   2 INT   3 QUIT  4 ILL   5 TRAP\n"
               " 6 ABRT  7 BUS   8 FPE   9 KILL 10 USR1\n"
               "11 SEGV 12 USR2 13 PIPE 14 ALRM 15 TERM\n");
        return 0;
    }
    if (strcmp(argv[1], "-s") == 0 && argc > 2) {
        const char *s = argv[2];
        sig = atoi(s);
        if (sig == 0 && strcmp(s, "0") != 0) {
            for (int i = 1; i < NSIG; i++) {
                if (strcasecmp(s, sig_name(i)) == 0) { sig = i; break; }
            }
        }
        a = 3;
    } else if (argv[1][0] == '-' && argv[1][1] != '\0' &&
               argv[1][1] >= '0' && argv[1][1] <= '9') {
        sig = atoi(argv[1] + 1);
        a = 2;
    }

    if (a >= argc) {
        fprintf(stderr, "kill: missing pid\n");
        return 1;
    }

    int ret = 0;
    for (; a < argc; a++) {
        pid_t pid = (pid_t)atoi(argv[a]);
        if (kill(pid, sig) < 0) {
            fprintf(stderr, "kill: %s: no such process\n", argv[a]);
            ret = 1;
        }
    }
    return ret;
}
