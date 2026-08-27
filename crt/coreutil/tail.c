/* tail - print the last N lines of a file */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAX_LINES 4096

static void tail_file(const char *name, int nlines)
{
    FILE f = name ? fopen(name, "r") : 0;
    if (name && f < 0) {
        fprintf(stderr, "tail: %s: no such file\n", name);
        return;
    }

    /* Read the whole file into memory (files here are small), remember
     * the offsets of every line start, then print the last nlines. */
    char *data = NULL;
    size_t cap = 0, len = 0;
    char buf[512];
    ssize_t r;
    while ((r = read(f, buf, sizeof(buf))) > 0) {
        if (len + (size_t)r + 1 > cap) {
            cap = cap ? cap * 2 : 1024;
            while (cap < len + (size_t)r + 1) cap *= 2;
            char *nd = realloc(data, cap);
            if (!nd) {
                free(data);
                if (name) close(f);
                fprintf(stderr, "tail: out of memory\n");
                return;
            }
            data = nd;
        }
        memcpy(data + len, buf, (size_t)r);
        len += (size_t)r;
    }
    if (len == 0) {
        free(data);
        if (name) close(f);
        return;
    }
    data[len] = '\0';

    static size_t starts[MAX_LINES];
    size_t n = 0;
    starts[n++] = 0;
    for (size_t i = 0; i < len; i++) {
        if (data[i] == '\n' && i + 1 < len && n < MAX_LINES)
            starts[n++] = i + 1;
    }

    size_t begin = (n <= (size_t)nlines) ? 0 : n - (size_t)nlines;
    for (size_t i = begin; i < n; i++) {
        size_t s = starts[i];
        size_t e = (i + 1 < n) ? starts[i + 1] : len;
        while (s < e && data[e - 1] == '\n')
            e--;
        for (size_t p = s; p < e; p++)
            putchar(data[p]);
        putchar('\n');
    }

    free(data);
    if (name)
        close(f);
}

int main(int argc, char *argv[])
{
    int nlines = 10;

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
        tail_file(NULL, nlines);
        return 0;
    }
    for (; a < argc; a++) {
        tail_file(argv[a], nlines);
    }
    return 0;
}
