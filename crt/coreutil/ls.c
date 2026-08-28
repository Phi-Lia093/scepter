/* ls - list directory contents */
#include "stdio.h"
#include "string.h"
#include "dirent.h"
#include "sys/stat.h"

static int long_fmt = 0;
static int all = 0;

static char type_char(uint8_t t)
{
    switch (t) {
    case DT_DIR:    return 'd';
    case DT_CHRDEV: return 'c';
    case DT_BLKDEV: return 'b';
    case DT_SYMLINK:return 'l';
    default:        return '-';
    }
}

static void list_one(const char *dirpath, const char *name)
{
    char full[512];
    if (strcmp(dirpath, "/") == 0)
        snprintf(full, sizeof(full), "/%s", name);
    else
        snprintf(full, sizeof(full), "%s/%s", dirpath, name);

    struct stat st;
    if (lstat(full, &st) < 0) {
        printf("%s\n", name);
        return;
    }

    if (long_fmt) {
        /* type + mode (r/w/x for u/g/o from st_mode) */
        char mode[11];
        mode[0] = type_char(st.st_type);
        mode[1] = (st.st_mode & 0400) ? 'r' : '-';
        mode[2] = (st.st_mode & 0200) ? 'w' : '-';
        mode[3] = (st.st_mode & 0100) ? 'x' : '-';
        mode[4] = (st.st_mode & 0040) ? 'r' : '-';
        mode[5] = (st.st_mode & 0020) ? 'w' : '-';
        mode[6] = (st.st_mode & 0010) ? 'x' : '-';
        mode[7] = (st.st_mode & 0004) ? 'r' : '-';
        mode[8] = (st.st_mode & 0002) ? 'w' : '-';
        mode[9] = (st.st_mode & 0001) ? 'x' : '-';
        mode[10] = '\0';
        printf("%s %8u %s%s\n", mode, st.st_size, name,
               st.st_type == DT_DIR ? "/" : "");
    } else {
        printf("%s%s\n", name, st.st_type == DT_DIR ? "/" : "");
    }
}

static int list_dir(const char *path)
{
    DIR *d = opendir(path);
    if (!d) {
        fprintf(stderr, "ls: %s: no such directory\n", path);
        return 1;
    }
    struct dirent *e;
    while ((e = readdir(d))) {
        if (!all && e->d_name[0] == '.')
            continue;   /* skip hidden entries + . and .. */
        list_one(path, e->d_name);
    }
    closedir(d);
    return 0;
}

int main(int argc, char *argv[])
{
    int a = 1;
    while (a < argc && argv[a][0] == '-' && argv[a][1]) {
        for (const char *f = argv[a] + 1; *f; f++) {
            if (*f == 'l') long_fmt = 1;
            else if (*f == 'a') all = 1;
            else {
                fprintf(stderr, "ls: unknown option: -%c\n", *f);
                return 1;
            }
        }
        a++;
    }

    if (a >= argc) {
        return list_dir(".");
    }

    int ret = 0;
    for (int i = a; i < argc; i++) {
        struct stat st;
        if (stat(argv[i], &st) == 0 && st.st_type == DT_DIR) {
            if (argc - a > 1)
                printf("%s:\n", argv[i]);
            list_dir(argv[i]);
        } else {
            list_one(".", argv[i]);   /* works for bare names too */
        }
        if (i < argc - 1 && argc - a > 1)
            printf("\n");
    }
    return ret;
}

