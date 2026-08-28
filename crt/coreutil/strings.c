/* strings - print printable strings in a binary file */
#include "stdio.h"
#include "string.h"
#include "unistd.h"
#include "fcntl.h"
#include "stdlib.h"

#define MIN_LEN 4

int main(int argc, char *argv[])
{
    int minlen = MIN_LEN;
    const char *path = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-n") == 0 && i + 1 < argc)
            minlen = atoi(argv[++i]);
        else
            path = argv[i];
    }

    int fd = path ? open(path, O_RDONLY) : dup(STDIN_FILENO);
    if (fd < 0) {
        fprintf(stderr, "strings: %s: cannot open\n", path ? path : "stdin");
        return 1;
    }

    char buf[4096];
    char run[512];
    int rlen = 0;
    long r;
    while ((r = read(fd, buf, sizeof(buf))) > 0) {
        for (long i = 0; i < r; i++) {
            unsigned char c = (unsigned char)buf[i];
            if (c >= 32 && c < 127) {
                if (rlen < (int)sizeof(run) - 1)
                    run[rlen++] = (char)c;
            } else {
                if (rlen >= minlen) {
                    run[rlen] = '\0';
                    printf("%s\n", run);
                }
                rlen = 0;
            }
        }
    }
    if (rlen >= minlen) {
        run[rlen] = '\0';
        printf("%s\n", run);
    }

    close(fd);
    return 0;
}
