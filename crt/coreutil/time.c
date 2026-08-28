/* time - time a command */
#include "stdio.h"
#include "unistd.h"
#include "string.h"
#include "sys/wait.h"
#include "sys/time.h"
#include "errno.h"

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "usage: time COMMAND [ARG...]\n");
        return 1;
    }

    struct timeval t0, t1;
    gettimeofday(&t0, NULL);

    pid_t pid = fork();
    if (pid < 0) {
        fprintf(stderr, "time: fork failed\n");
        return 1;
    }
    if (pid == 0) {
        execvp(argv[1], &argv[1]);
        fprintf(stderr, "time: %s: command not found\n", argv[1]);
        _exit(127);
    }

    int status = 0;
    waitpid(pid, &status, 0);

    gettimeofday(&t1, NULL);
    long usec = (t1.tv_sec - t0.tv_sec) * 1000000L + (t1.tv_usec - t0.tv_usec);

    fprintf(stderr, "real\t%ld.%06lds\n", usec / 1000000, usec % 1000000);

    if (WIFEXITED(status))
        return WEXITSTATUS(status);
    return 1;
}
