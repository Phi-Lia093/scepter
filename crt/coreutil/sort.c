/* sort - sort lines of text */
#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "unistd.h"
#include "fcntl.h"

static int reverse = 0;

static int cmp_lines(const void *a, const void *b)
{
    const char *const *sa = (const char *const *)a;
    const char *const *sb = (const char *const *)b;
    int r = strcmp(*sa, *sb);
    return reverse ? -r : r;
}

int main(int argc, char *argv[])
{
    const char *path = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-r") == 0)
            reverse = 1;
        else
            path = argv[i];
    }

    FILE in = stdin;
    if (path) {
        in = open(path, O_RDONLY);
        if (in < 0) {
            fprintf(stderr, "sort: %s: no such file\n", path);
            return 1;
        }
    }

    char **lines = NULL;
    int n = 0, cap = 0;

    char *line = NULL;
    size_t lcap = 0;
    ssize_t len;
    while ((len = getline(&line, &lcap, in)) > 0) {
        /* strip trailing newline */
        if (line[len - 1] == '\n')
            line[len - 1] = '\0';
        if (n == cap) {
            cap = cap ? cap * 2 : 64;
            lines = (char **)realloc(lines, (size_t)cap * sizeof(char *));
            if (!lines) return 1;
        }
        lines[n] = strdup(line);
        if (!lines[n]) return 1;
        n++;
    }
    free(line);

    qsort(lines, (size_t)n, sizeof(char *), cmp_lines);

    for (int i = 0; i < n; i++) {
        printf("%s\n", lines[i]);
        free(lines[i]);
    }
    free(lines);

    if (path) close(in);
    return 0;
}
