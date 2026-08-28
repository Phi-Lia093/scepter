/* wc - count lines, words, and bytes */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int count_lines = 1, count_words = 1, count_bytes = 1;

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

    if (count_lines && count_words && count_bytes)
        printf("%7lu %7lu %7lu %s\n", lines, words, bytes,
               name ? name : "");
    else {
        if (count_lines) printf("%7lu ", lines);
        if (count_words) printf("%7lu ", words);
        if (count_bytes) printf("%7lu ", bytes);
        printf("%s\n", name ? name : "");
    }
}

int main(int argc, char *argv[])
{
    unsigned long tl = 0, tw = 0, tb = 0;
    int ok = 1;

    /* Parse flags: -l -w -c (and combinations like -lw) */
    int a = 1;
    while (a < argc && argv[a][0] == '-') {
        const char *f = argv[a] + 1;
        if (*f == '\0')
            break;
        count_lines = count_words = count_bytes = 0;
        for (; *f; f++) {
            if (*f == 'l') count_lines = 1;
            else if (*f == 'w') count_words = 1;
            else if (*f == 'c') count_bytes = 1;
            else {
                fprintf(stderr, "wc: unknown option: -%c\n", *f);
                return 1;
            }
        }
        a++;
    }

    if (a >= argc) {
        wc_file(NULL, &tl, &tw, &tb, &ok);   /* prints the counts */
        return ok ? 0 : 1;
    }

    for (; a < argc; a++)
        wc_file(argv[a], &tl, &tw, &tb, &ok);

    if (argc > 2)
        printf("%7lu %7lu %7lu total\n", tl, tw, tb);
    return ok ? 0 : 1;
}
