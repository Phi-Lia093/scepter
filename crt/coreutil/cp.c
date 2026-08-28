/* cp - copy files and directories */
#include "stdio.h"
#include "unistd.h"
#include "fcntl.h"
#include "string.h"
#include "sys/stat.h"
#include "dirent.h"

static int recursive = 0;

static int copy_file(const char *src, const char *dst)
{
    int in = open(src, O_RDONLY);
    if (in < 0) {
        fprintf(stderr, "cp: %s: no such file\n", src);
        return -1;
    }
    int out = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (out < 0) {
        fprintf(stderr, "cp: %s: cannot create\n", dst);
        close(in);
        return -1;
    }
    char buf[1024];
    int n;
    while ((n = read(in, buf, sizeof buf)) > 0)
        write(out, buf, n);
    close(in);
    close(out);
    return 0;
}

static int copy_tree(const char *src, const char *dst)
{
    struct stat st;
    if (lstat(src, &st) < 0) {
        fprintf(stderr, "cp: %s: no such file\n", src);
        return -1;
    }

    if (st.st_type != DT_DIR)
        return copy_file(src, dst);

    /* create destination directory */
    mkdir(dst, 0777);

    DIR *d = opendir(src);
    if (!d)
        return -1;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.' &&
            (e->d_name[1] == '\0' ||
             (e->d_name[1] == '.' && e->d_name[2] == '\0')))
            continue;
        char s[300], t[300];
        snprintf(s, sizeof(s), "%s/%s", src, e->d_name);
        snprintf(t, sizeof(t), "%s/%s", dst, e->d_name);
        copy_tree(s, t);
    }
    closedir(d);
    return 0;
}

int main(int argc, char *argv[])
{
    int a = 1;
    if (argc > 1 && strcmp(argv[1], "-r") == 0) {
        recursive = 1;
        a = 2;
    }
    if (argc - a < 2) {
        fprintf(stderr, "usage: cp [-r] src dst\n");
        return 1;
    }

    /* If destination is a directory, copy into it preserving names. */
    struct stat dst_st;
    if (stat(argv[argc - 1], &dst_st) == 0 && dst_st.st_type == DT_DIR) {
        for (int i = a; i < argc - 1; i++) {
            char full[300];
            const char *base = strrchr(argv[i], '/');
            base = base ? base + 1 : argv[i];
            snprintf(full, sizeof(full), "%s/%s", argv[argc - 1], base);
            if (recursive)
                copy_tree(argv[i], full);
            else
                copy_file(argv[i], full);
        }
        return 0;
    }

    if (argc - a > 2) {
        fprintf(stderr, "cp: %s: not a directory\n", argv[argc - 1]);
        return 1;
    }

    if (recursive)
        return copy_tree(argv[a], argv[a + 1]);
    return copy_file(argv[a], argv[a + 1]);
}

