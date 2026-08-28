/* df - report filesystem disk space usage */
#include "stdio.h"
#include "string.h"
#include "unistd.h"
#include "fcntl.h"
#include "stdlib.h"
#include "sys/vfs.h"

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    printf("%-10s %10s %10s %10s %4s %s\n",
           "Filesystem", "1K-blocks", "Used", "Available", "Use%", "Mounted on");

    /* Enumerate /proc/mounts; statfs each mount point. */
    int m = open("/proc/mounts", O_RDONLY);
    if (m < 0) {
        /* fall back to just the root */
        struct statfs sb;
        if (statfs("/", &sb) < 0) {
            fprintf(stderr, "df: statfs failed\n");
            return 1;
        }
        unsigned long bs = sb.f_bsize / 1024;
        unsigned long total = (unsigned long)sb.f_blocks * bs;
        unsigned long free_ = (unsigned long)sb.f_bfree * bs;
        unsigned long used = total - free_;
        printf("%-10s %10lu %10lu %10lu %3lu%% %s\n",
               "rootfs", total, used, free_,
               total ? (used * 100) / total : 0, "/");
        return 0;
    }

    char *line = NULL;
    size_t cap = 0;
    int printed = 0;
    while (getline(&line, &cap, m) > 0) {
        char *save = NULL;
        char *dev = strtok_r(line, " \t\n", &save);
        char *mp  = strtok_r(NULL, " \t\n", &save);
        strtok_r(NULL, " \t\n", &save);   /* fstype */
        if (dev && mp) {
            struct statfs sb;
            if (statfs(mp, &sb) == 0) {
                unsigned long bs = sb.f_bsize / 1024;
                unsigned long total = (unsigned long)sb.f_blocks * bs;
                unsigned long free_ = (unsigned long)sb.f_bfree * bs;
                unsigned long used = total - free_;
                printf("%-10s %10lu %10lu %10lu %3lu%% %s\n",
                       dev, total, used, free_,
                       total ? (used * 100) / total : 0, mp);
                printed = 1;
            }
        }
    }
    free(line);
    close(m);

    if (!printed) {
        struct statfs sb;
        if (statfs("/", &sb) == 0) {
            unsigned long bs = sb.f_bsize / 1024;
            unsigned long total = (unsigned long)sb.f_blocks * bs;
            unsigned long free_ = (unsigned long)sb.f_bfree * bs;
            printf("%-10s %10lu %10lu %10lu %3lu%% %s\n",
                   "rootfs", total, total - free_, free_,
                   total ? ((total - free_) * 100) / total : 0, "/");
        }
    }
    return 0;
}
