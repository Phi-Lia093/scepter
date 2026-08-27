/* wc - count lines, words, and bytes */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void wc_file(const char *name, unsigned long *lines_out,
                    unsigned long *words_out, unsigned long *bytes_out,
                    int *ok)
{
    FILE f = name ? fopen(name, "r") : 0;
    if (name && f < 0) {
        fprintf(stderr, "wc: %s: no such file\n", name);
        *ok = 0;
        return;
    }

    unsigned long lines = 0, words = 0, bytes = 0;
    char buf[512];
    int in_word = 0;
    ssize_t r;
    while ((r = read(f, buf, sizeof(buf))) > 0) {
        for (ssize_t i = 0; i < r; i++) {
            char c = buf[i];
            bytes++;
            if (c == '\n')
                lines++;
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                in_word = 0;
            } else if (!in_word) {
                in_word = 1;
                words++;
            }
        }
    }
    if (name)
        close(f);

    *lines_out += lines;
    *words_out += words;
    *bytes_out += bytes;

    printf("%7lu %7lu %7lu %s\n", lines, words, bytes,
           name ? name : "");
}

int main(int argc, char *argv[])
{
    unsigned long tl = 0, tw = 0, tb = 0;
    int ok = 1;

    if (argc == 1) {
        wc_file(NULL, &tl, &tw, &tb, &ok);
        printf("%7lu %7lu %7lu\n", tl, tw, tb);
        return ok ? 0 : 1;
    }

    for (int a = 1; a < argc; a++)
        wc_file(argv[a], &tl, &tw, &tb, &ok);

    if (argc > 2)
        printf("%7lu %7lu %7lu total\n", tl, tw, tb);
    return ok ? 0 : 1;
}
