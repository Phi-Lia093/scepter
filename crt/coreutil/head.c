/* head - print the first N lines of files */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define BUFSZ 4096

static void head_file(const char *name, int nlines, int show_name)
{
    FILE f = 0;
    if (name) {
        f = fopen(name, "r");
        if (f < 0) {
            fprintf(stderr, "head: %s: no such file\n", name);
            return;
        }
    } else {
        f = 0;   /* stdin */
    }

    if (show_name && name)
        printf("==> %s <==\n", name);

    char buf[BUFSZ];
    int lines = 0;
    ssize_t r;
    while ((r = read(f, buf, BUFSZ)) > 0 && lines < nlines) {
        for (ssize_t i = 0; i < r; i++) {
            if (lines >= nlines) break;
            putchar(buf[i]);
            if (buf[i] == '\n')
                lines++;
        }
    }
    if (name)
        close(f);
}

int main(int argc, char *argv[])
{
    int nlines = 10;
    int show_name = 0;

    int a = 1;
    while (a < argc && argv[a][0] == '-') {
        if (argv[a][1] == 'n' && argv[a][2] == '\0' && a + 1 < argc) {
            nlines = atoi(argv[a + 1]);
            a += 2;
        } else {
            nlines = atoi(&argv[a][1]);
            a++;
        }
    }

    if (a == argc) {
        head_file(NULL, nlines, 0);
        return 0;
    }
    if (argc - a > 1)
        show_name = 1;

    for (; a < argc; a++) {
        head_file(argv[a], nlines, show_name);
        if (a + 1 < argc)
            printf("\n");
    }
    return 0;
}
