/* uniq - report or omit repeated adjacent lines */
#include "stdio.h"
#include "string.h"
#include "unistd.h"
#include "fcntl.h"
#include "stdlib.h"

int main(int argc, char *argv[])
{
    int only_dup = 0, count = 0, only_uniq = 0;
    const char *path = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-d") == 0) only_dup = 1;
        else if (strcmp(argv[i], "-c") == 0) count = 1;
        else if (strcmp(argv[i], "-u") == 0) only_uniq = 1;
        else path = argv[i];
    }

    FILE in = stdin;
    if (path) {
        in = open(path, O_RDONLY);
        if (in < 0) {
            fprintf(stderr, "uniq: %s: no such file\n", path);
            return 1;
        }
    }

    char *prev = NULL;
    int prev_count = 0;
    char *line = NULL;
    size_t lcap = 0;

    while (getline(&line, &lcap, in) > 0) {
        if (prev && strcmp(prev, line) == 0) {
            prev_count++;
            continue;
        }
        /* flush previous group */
        if (prev) {
            int emit = (!only_uniq && !only_dup) || (only_dup && prev_count > 1)
                       || (only_uniq && prev_count == 1);
            if (emit) {
                if (count)
                    printf("%7d %s", prev_count, prev);
                else
                    fputs(prev, stdout);
            }
        }
        free(prev);
        prev = strdup(line);
        prev_count = 1;
    }
    if (prev) {
        int emit = (!only_uniq && !only_dup) || (only_dup && prev_count > 1)
                   || (only_uniq && prev_count == 1);
        if (emit) {
            if (count)
                printf("%7d %s", prev_count, prev);
            else
                fputs(prev, stdout);
        }
        free(prev);
    }
    free(line);

    if (path) close(in);
    return 0;
}
