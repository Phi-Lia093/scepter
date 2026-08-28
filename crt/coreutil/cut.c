/* cut - extract selected fields from each line */
#include "stdio.h"
#include "string.h"
#include "unistd.h"
#include "fcntl.h"
#include "stdlib.h"

static char delim = '\t';
static int field1 = 1, field2 = 1;

static int parse_range(const char *s)
{
    /* supports "N", "N-", "-M", "N-M" */
    const char *dash = strchr(s, '-');
    if (!dash) {
        int n = atoi(s);
        if (n < 1) return -1;
        field1 = field2 = n;
        return 0;
    }
    if (dash == s) {
        field1 = 1;
        field2 = atoi(dash + 1);
        if (field2 < 1) return -1;
    } else if (dash[1] == '\0') {
        field1 = atoi(s);
        if (field1 < 1) return -1;
        field2 = 999999;
    } else {
        field1 = atoi(s);
        field2 = atoi(dash + 1);
        if (field1 < 1 || field2 < 1 || field1 > field2) return -1;
    }
    return 0;
}

int main(int argc, char *argv[])
{
    const char *path = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-d") == 0 && i + 1 < argc) {
            delim = argv[++i][0];
        } else if (strcmp(argv[i], "-f") == 0 && i + 1 < argc) {
            if (parse_range(argv[++i]) < 0) {
                fprintf(stderr, "cut: invalid field list\n");
                return 1;
            }
        } else {
            path = argv[i];
        }
    }

    FILE in = stdin;
    if (path) {
        in = open(path, O_RDONLY);
        if (in < 0) {
            fprintf(stderr, "cut: %s: no such file\n", path);
            return 1;
        }
    }

    char *line = NULL;
    size_t cap = 0;
    while (getline(&line, &cap, in) > 0) {
        /* split on delim */
        char *fields[256];
        int nf = 0;
        char *p = line;
        fields[nf++] = p;
        while (*p && nf < 256) {
            if (*p == delim) {
                *p = '\0';
                fields[nf++] = p + 1;
            }
            p++;
        }
        int first = 1;
        for (int i = field1; i <= field2 && i <= nf; i++) {
            if (!first) putchar(delim);
            fputs(fields[i - 1], stdout);
            first = 0;
        }
        putchar('\n');
    }
    free(line);
    if (path) close(in);
    return 0;
}
