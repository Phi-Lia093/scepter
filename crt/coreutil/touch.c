/* touch - update file timestamps, or create empty files */
#include "stdio.h"
#include "unistd.h"
#include "fcntl.h"
#include "utime.h"
#include <string.h>

int main(int argc, char *argv[])
{
    int no_create = 0;
    int a = 1;
    if (argc > 1 && strcmp(argv[1], "-c") == 0) {
        no_create = 1;
        a = 2;
    }
    if (a >= argc) {
        fprintf(stderr, "usage: touch [-c] file...\n");
        return 1;
    }

    struct utimbuf ut;
    ut.actime = 0;      /* "now" */
    ut.modtime = 0;

    for (; a < argc; a++) {
        if (utime(argv[a], &ut) < 0) {
            if (no_create) {
                /* missing files are fine with -c */
                continue;
            }
            int fd = open(argv[a], O_WRONLY | O_CREAT, 0644);
            if (fd < 0) {
                fprintf(stderr, "touch: %s: cannot create\n", argv[a]);
                return 1;
            }
            close(fd);
            utime(argv[a], &ut);
        }
    }
    return 0;
}
