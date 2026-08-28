/* od - dump files in octal/hexadecimal */
#include "stdio.h"
#include "string.h"
#include "unistd.h"
#include "fcntl.h"

static int fmt = 'o';   /* o, x, or c */
static int group = 1;   /* bytes per unit */

static void dump_bytes(const unsigned char *buf, int n, long offset)
{
    if (fmt == 'c') {
        printf("%07lo ", offset);
        for (int i = 0; i < n; i++) {
            unsigned char c = buf[i];
            if (c >= 32 && c < 127)
                printf(" %3c ", c);
            else if (c == '\n')
                printf("  \\n ");
            else if (c == '\t')
                printf("  \\t ");
            else
                printf(" %3o ", c);
        }
        printf("\n");
        return;
    }

    printf("%07lo ", offset);
    for (int i = 0; i < n; i += group) {
        unsigned int v = 0;
        for (int j = 0; j < group && i + j < n; j++)
            v = (v << 8) | buf[i + j];
        if (fmt == 'o')
            printf(" %0*o", group * 3, v);
        else
            printf(" %0*x", group * 2, v);
    }
    printf("\n");
}

int main(int argc, char *argv[])
{
    const char *path = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-t") == 0 && i + 1 < argc) {
            const char *t = argv[++i];
            if (strcmp(t, "x1") == 0) { fmt = 'x'; group = 1; }
            else if (strcmp(t, "x2") == 0) { fmt = 'x'; group = 2; }
            else if (strcmp(t, "x4") == 0) { fmt = 'x'; group = 4; }
            else if (strcmp(t, "o1") == 0) { fmt = 'o'; group = 1; }
            else if (strcmp(t, "o2") == 0) { fmt = 'o'; group = 2; }
            else if (strcmp(t, "c") == 0) { fmt = 'c'; group = 1; }
        } else {
            path = argv[i];
        }
    }

    int fd = path ? open(path, O_RDONLY) : dup(STDIN_FILENO);
    if (fd < 0) {
        fprintf(stderr, "od: %s: cannot open\n", path ? path : "stdin");
        return 1;
    }

    unsigned char buf[16];
    long offset = 0;
    long r;
    while ((r = read(fd, buf, sizeof(buf))) > 0) {
        dump_bytes(buf, (int)r, offset);
        offset += r;
    }
    close(fd);
    return 0;
}
