/* watch - run a command periodically */
#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "unistd.h"
#include "sys/ioctl.h"
#include "sys/wait.h"

int main(int argc, char *argv[])
{
    int interval = 2;
    int a = 1;
    if (argc > 2 && strcmp(argv[1], "-n") == 0) {
        interval = atoi(argv[2]);
        a = 3;
    }
    if (a >= argc) {
        fprintf(stderr, "usage: watch [-n SECONDS] COMMAND [ARG...]\n");
        return 1;
    }

    for (;;) {
        ioctl(STDOUT_FILENO, IOCTL_TTY_CLEAR, 0);
        printf("Every %ds: ", interval);
        for (int i = a; i < argc; i++)
            printf("%s%s", i == a ? "" : " ", argv[i]);
        printf("\n\n");

        pid_t pid = fork();
        if (pid == 0) {
            execvp(argv[a], &argv[a]);
            fprintf(stderr, "watch: %s: command not found\n", argv[a]);
            _exit(127);
        }
        int st;
        waitpid(pid, &st, 0);
        sleep((unsigned int)interval);
    }
    return 0;
}
