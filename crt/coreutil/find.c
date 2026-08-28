/* find - search for files in a directory hierarchy */
#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "dirent.h"
#include "sys/stat.h"
#include "unistd.h"

static const char *name_glob = NULL;
static int type_filter = 0;   /* DT_* or 0 for any */
static int maxdepth = -1;

static int name_matches(const char *name);
static int name_matches_tail(const char *s, const char *g);

static int name_matches(const char *name)
{
    if (!name_glob)
        return 1;
    /* simple glob: * and ? only */
    const char *g = name_glob;
    const char *s = name;
    while (*g) {
        if (*g == '*') {
            g++;
            if (!*g)
                return 1;
            for (const char *try = s; ; try++) {
                if (name_matches_tail(try, g))
                    return 1;
                if (!*try)
                    return 0;
            }
        } else if (*g == '?') {
            if (!*s)
                return 0;
            g++;
            s++;
        } else {
            if (*s != *g)
                return 0;
            g++;
            s++;
        }
    }
    return *s == '\0';
}

int name_matches_tail(const char *s, const char *g)
{
    if (!*g)
        return 1;
    if (*g == '*')
        return name_matches_tail(s, g + 1) || (*s && name_matches_tail(s + 1, g));
    if (*g == '?')
        return *s && name_matches_tail(s + 1, g + 1);
    if (*s != *g)
        return 0;
    return name_matches_tail(s + 1, g + 1);
}

static int walk(const char *path, int depth)
{
    struct stat st;
    if (lstat(path, &st) < 0)
        return 0;

    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;

    int type_ok = !type_filter || st.st_type == type_filter;
    if (type_ok && name_matches(base))
        printf("%s\n", path);

    if (st.st_type != DT_DIR)
        return 0;
    if (maxdepth >= 0 && depth >= maxdepth)
        return 0;

    DIR *d = opendir(path);
    if (!d)
        return 0;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.' &&
            (e->d_name[1] == '\0' ||
             (e->d_name[1] == '.' && e->d_name[2] == '\0')))
            continue;
        char full[512];
        snprintf(full, sizeof(full), "%s/%s", path, e->d_name);
        walk(full, depth + 1);
    }
    closedir(d);
    return 0;
}

int main(int argc, char *argv[])
{
    const char *start = ".";
    int a = 1;
    if (a < argc && argv[a][0] == '-') {
        fprintf(stderr, "find: bad option %s\n", argv[a]);
        return 1;
    }
    if (a < argc && argv[a][0] != '-') {
        start = argv[a++];
    }
    while (a < argc) {
        if (strcmp(argv[a], "-name") == 0 && a + 1 < argc) {
            name_glob = argv[a + 1];
            a += 2;
        } else if (strcmp(argv[a], "-type") == 0 && a + 1 < argc) {
            if (strcmp(argv[a + 1], "f") == 0) type_filter = DT_REG;
            else if (strcmp(argv[a + 1], "d") == 0) type_filter = DT_DIR;
            else if (strcmp(argv[a + 1], "l") == 0) type_filter = DT_SYMLINK;
            else type_filter = DT_UNKNOWN;
            a += 2;
        } else if (strcmp(argv[a], "-maxdepth") == 0 && a + 1 < argc) {
            maxdepth = atoi(argv[a + 1]);
            a += 2;
        } else {
            fprintf(stderr, "find: unknown predicate: %s\n", argv[a]);
            return 1;
        }
    }

    walk(start, 0);
    return 0;
}
