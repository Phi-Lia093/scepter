/* grep - search text for patterns */
#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "unistd.h"
#include "fcntl.h"
#include "dirent.h"

static const char *pattern = NULL;
static int ignore_case = 0;
static int show_lineno = 0;
static int invert = 0;
static int recursive = 0;
static int found = 0;

static int matches(const char *line)
{
    int hit;
    if (ignore_case) {
        /* simple case-insensitive substring */
        size_t lp = strlen(pattern), ll = strlen(line);
        hit = 0;
        for (size_t i = 0; i + lp <= ll && !hit; i++) {
            size_t j = 0;
            for (; j < lp; j++) {
                char a = line[i + j], b = pattern[j];
                if (a >= 'A' && a <= 'Z') a += 32;
                if (b >= 'A' && b <= 'Z') b += 32;
                if (a != b) break;
            }
            if (j == lp) hit = 1;
        }
    } else {
        hit = strstr(line, pattern) != NULL;
    }
    return invert ? !hit : hit;
}

static int search_file(const char *path, int print_name, int dir_ctx)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return 0;

    char *line = NULL;
    size_t cap = 0;
    int lineno = 0;

    while (getline(&line, &cap, fd) > 0) {
        lineno++;
        if (matches(line)) {
            found = 1;
            if (print_name)
                printf("%s:", path);
            if (show_lineno)
                printf("%d:", lineno);
            fputs(line, stdout);
            if (line[strlen(line) - 1] != '\n')
                putchar('\n');
        }
    }
    free(line);
    close(fd);
    (void)dir_ctx;
    return 0;
}

static int search_dir(const char *path)
{
    DIR *d = opendir(path);
    if (!d)
        return 0;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.')
            continue;
        char full[300];
        snprintf(full, sizeof(full), "%s/%s", path, e->d_name);
        if (e->d_type == DT_DIR)
            search_dir(full);
        else
            search_file(full, 1, 0);
    }
    closedir(d);
    return 0;
}

int main(int argc, char *argv[])
{
    int a = 1;
    while (a < argc && argv[a][0] == '-') {
        const char *op = argv[a];
        if (op[1] == 'i') ignore_case = 1;
        else if (op[1] == 'n') show_lineno = 1;
        else if (op[1] == 'v') invert = 1;
        else if (op[1] == 'r') recursive = 1;
        else {
            fprintf(stderr, "grep: unknown option: %s\n", op);
            return 2;
        }
        a++;
    }

    if (a >= argc) {
        fprintf(stderr, "usage: grep [-invr] PATTERN [FILE...]\n");
        return 2;
    }
    pattern = argv[a++];

    if (a >= argc) {
        /* stdin */
        char *line = NULL;
        size_t cap = 0;
        int lineno = 0;
        while (getline(&line, &cap, stdin) > 0) {
            lineno++;
            if (matches(line)) {
                found = 1;
                if (show_lineno)
                    printf("%d:", lineno);
                fputs(line, stdout);
                if (line[strlen(line) - 1] != '\n')
                    putchar('\n');
            }
        }
        free(line);
        return found ? 0 : 1;
    }

    int multiple = (argc - a) > 1;
    for (; a < argc; a++) {
        if (recursive)
            search_dir(argv[a]);
        else
            search_file(argv[a], multiple, 0);
    }
    return found ? 0 : 1;
}
