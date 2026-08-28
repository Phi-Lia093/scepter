/* top - periodically display process information */
#include "stdio.h"
#include "string.h"
#include "stdlib.h"
#include "unistd.h"
#include "fcntl.h"
#include "sys/ioctl.h"
#include "time.h"

static void print_tasks(void)
{
    int fd = open("/proc/tasks", O_RDONLY);
    if (fd < 0)
        return;
    char *line = NULL;
    size_t cap = 0;
    while (getline(&line, &cap, fd) > 0) {
        /* pid ppid state name prio uticks sticks */
        char *save = NULL;
        char *pid = strtok_r(line, " \t\n", &save);
        char *ppid = strtok_r(NULL, " \t\n", &save);
        char *state = strtok_r(NULL, " \t\n", &save);
        char *name = strtok_r(NULL, " \t\n", &save);
        if (pid && state && name)
            printf("%5s %5s %-8s %s\n", pid, ppid, state, name);
    }
    free(line);
    close(fd);
}

int main(int argc, char *argv[])
{
    int interval = 2;
    if (argc > 1)
        interval = atoi(argv[1]);
    if (interval < 1)
        interval = 1;

    for (;;) {
        time_t t = time(NULL);
        printf("Scepter processes - %s\n", ctime(&t));
        printf("%5s %5s %-8s %s\n", "PID", "PPID", "STATE", "NAME");
        print_tasks();
        sleep((unsigned int)interval);
        ioctl(STDOUT_FILENO, IOCTL_TTY_CLEAR, 0);
    }
    return 0;
}
