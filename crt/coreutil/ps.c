/* ps - list processes (reads /proc/tasks) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

int main(int argc, char *argv[])
{
    (void)argc; (void)argv;

    int fd = open("/proc/tasks", O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "ps: cannot open /proc/tasks (is /proc mounted?)\n");
        return 1;
    }

    printf("%-5s %-5s %-8s %-16s %-4s %-8s %-8s\n",
           "PID", "PPID", "STATE", "NAME", "PRIO", "UTICKS", "STICKS");

    char *line = NULL;
    size_t n = 0;
    while (getline(&line, &n, fd) > 0) {
        char *save = NULL;
        char *t;
        const char *fields[7];
        int nf = 0;

        t = strtok_r(line, " \t\n", &save);
        while (t && nf < 7) {
            fields[nf++] = t;
            t = strtok_r(NULL, " \t\n", &save);
        }
        if (nf < 7) continue;

        printf("%-5s %-5s %-8s %-16s %-4s %-8s %-8s\n",
               fields[0], fields[1], fields[2], fields[3], fields[4],
               fields[5], fields[6]);
    }
    free(line);
    close(fd);
    return 0;
}
