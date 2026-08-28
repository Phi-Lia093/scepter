/* mount - mount filesystems */
#include "stdio.h"
#include "string.h"
#include "stdlib.h"
#include "unistd.h"
#include "fcntl.h"
#include "sys/mount.h"

int main(int argc, char *argv[])
{
    if (argc == 1) {
        /* list mounts from /proc/mounts */
        int fd = open("/proc/mounts", O_RDONLY);
        if (fd < 0) {
            fprintf(stderr, "mount: cannot read /proc/mounts\n");
            return 1;
        }
        char *line = NULL;
        size_t cap = 0;
        while (getline(&line, &cap, fd) > 0)
            fputs(line, stdout);
        free(line);
        close(fd);
        return 0;
    }

    /* mount [ -t fstype ] DEVICE DIR  or  mount DEVICE DIR */
    int a = 1;
    const char *fstype = NULL;
    while (a < argc && argv[a][0] == '-') {
        if (strcmp(argv[a], "-t") == 0 && a + 1 < argc) {
            fstype = argv[a + 1];
            a += 2;
        } else {
            fprintf(stderr, "mount: unknown option: %s\n", argv[a]);
            return 1;
        }
    }
    if (argc - a < 2) {
        fprintf(stderr, "usage: mount [-t TYPE] DEVICE DIR\n");
        return 1;
    }

    /* Without -t, guess from the device (devfs -> devfs, else ext2). */
    if (!fstype) {
        if (strstr(argv[a], "devfs") || strcmp(argv[a], "/dev") == 0 ||
            strcmp(argv[a], "none") == 0)
            fstype = "devfs";
        else
            fstype = "ext2";
    }

    if (mount(argv[a], argv[a + 1], fstype, 0, NULL) < 0) {
        fprintf(stderr, "mount: cannot mount %s on %s\n", argv[a], argv[a + 1]);
        return 1;
    }
    return 0;
}
