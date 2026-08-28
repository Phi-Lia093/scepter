/* killall - kill processes by name */
#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "unistd.h"
#include "fcntl.h"
#include "signal.h"

int main(int argc, char *argv[])
{
    int sig = SIGTERM;
    int a = 1;
    if (argc > 1 && argv[1][0] == '-') {
        sig = atoi(argv[1] + 1);
        if (sig == 0) sig = SIGTERM;
        a = 2;
    }
    if (a >= argc) {
        fprintf(stderr, "usage: killall [-SIGNAL] NAME...\n");
        return 1;
    }

    int fd = open("/proc/tasks", O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "killall: cannot read /proc/tasks\n");
        return 1;
    }

    int killed = 0;
    char *line = NULL;
    size_t cap = 0;
    while (getline(&line, &cap, fd) > 0) {
        char *save = NULL;
        strtok_r(line, " \t\n", &save);   /* pid */
        strtok_r(NULL, " \t\n", &save);   /* ppid */
        strtok_r(NULL, " \t\n", &save);   /* state */
        char *name = strtok_r(NULL, " \t\n", &save);
        if (!name)
            continue;
        for (int i = a; i < argc; i++) {
            if (strcmp(name, argv[i]) == 0) {
                char *save2 = NULL;
                /* re-tokenize from the start to get the pid */
                char copy[256];
                snprintf(copy, sizeof(copy), "%s", line);
                char *pid_s = strtok_r(copy, " \t\n", &save2);
                kill((pid_t)atoi(pid_s), sig);
                killed++;
            }
        }
    }
    free(line);
    close(fd);
    return killed ? 0 : 1;
}
