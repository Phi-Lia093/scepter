/* rm - remove files or directories */
#include "stdio.h"
#include "unistd.h"
#include "string.h"
#include "sys/stat.h"
#include "dirent.h"

static int force = 0;

static int remove_tree(const char *path)
{
    struct stat st;
    if (lstat(path, &st) < 0)
        return force ? 0 : -1;

    if (st.st_type == DT_DIR) {
        DIR *d = opendir(path);
        if (d) {
            struct dirent *e;
            while ((e = readdir(d))) {
                if (e->d_name[0] == '.' &&
                    (e->d_name[1] == '\0' ||
                     (e->d_name[1] == '.' && e->d_name[2] == '\0')))
                    continue;
                char full[512];
                snprintf(full, sizeof(full), "%s/%s", path, e->d_name);
                remove_tree(full);
            }
            closedir(d);
        }
        return rmdir(path);
    }
    return unlink(path);
}

int main(int argc, char *argv[])
{
    int recursive = 0;
    int a = 1;
    while (a < argc && argv[a][0] == '-' && argv[a][1]) {
        for (const char *f = argv[a] + 1; *f; f++) {
            if (*f == 'r') recursive = 1;
            else if (*f == 'f') force = 1;
            else {
                fprintf(stderr, "rm: unknown option: -%c\n", *f);
                return 1;
            }
        }
        a++;
    }

    if (a >= argc) {
        fprintf(stderr, "usage: rm [-rf] file...\n");
        return 1;
    }

    int ret = 0;
    for (int i = a; i < argc; i++) {
        struct stat st;
        if (lstat(argv[i], &st) == 0 && st.st_type == DT_DIR) {
            if (!recursive) {
                fprintf(stderr, "rm: %s: is a directory (use -r)\n", argv[i]);
                ret = 1;
                continue;
            }
            if (remove_tree(argv[i]) < 0) {
                fprintf(stderr, "rm: %s: cannot remove\n", argv[i]);
                ret = 1;
            }
        } else {
            if (unlink(argv[i]) < 0 && !force) {
                fprintf(stderr, "rm: %s: failed\n", argv[i]);
                ret = 1;
            }
        }
    }
    return ret;
}

