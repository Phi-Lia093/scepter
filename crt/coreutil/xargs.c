/* xargs - build and execute command lines from standard input */
#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "unistd.h"
#include "sys/wait.h"

#define MAX_ARGV 64

int main(int argc, char *argv[])
{
    int a = 1;
    const char *cmd = "echo";   /* POSIX default is echo */
    char *cmd_argv[MAX_ARGV];
    int ca = 0;

    /* First non-option argument is the command; build a fixed argv. */
    if (a < argc && argv[a][0] != '-') {
        cmd = argv[a];
        a++;
    }
    cmd_argv[ca++] = (char *)cmd;
    while (a < argc && ca < MAX_ARGV - 2)
        cmd_argv[ca++] = argv[a++];

    /* Read stdin, collect up to the free argv slots per invocation. */
    int slot_free = MAX_ARGV - ca - 1;

    char *line = NULL;
    size_t cap = 0;
    int have = 0;

    while (getline(&line, &cap, stdin) > 0) {
        /* strip newline */
        size_t len = strlen(line);
        if (len && line[len - 1] == '\n')
            line[len - 1] = '\0';
        if (line[0] == '\0')
            continue;

        cmd_argv[ca + have] = strdup(line);
        have++;
        cmd_argv[ca + have] = NULL;

        if (have >= slot_free) {
            pid_t pid = fork();
            if (pid == 0) {
                execve(cmd, cmd_argv, environ);
                fprintf(stderr, "xargs: %s: exec failed\n", cmd);
                exit(126);
            }
            int st;
            waitpid(pid, &st, 0);
            for (int i = 0; i < have; i++)
                free(cmd_argv[ca + i]);
            have = 0;
        }
    }

    if (have > 0) {
        cmd_argv[ca + have] = NULL;
        pid_t pid = fork();
        if (pid == 0) {
            execve(cmd, cmd_argv, environ);
            fprintf(stderr, "xargs: %s: exec failed\n", cmd);
            exit(126);
        }
        int st;
        waitpid(pid, &st, 0);
    }

    free(line);
    return 0;
}
