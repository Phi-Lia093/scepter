/* ln - create hard or symbolic links */
#include "stdio.h"
#include "unistd.h"
#include <string.h>
#include <sys/stat.h>

static void usage(void)
{
    fprintf(stderr, "usage: ln [-s] TARGET LINK\n"
                    "       ln [-s] TARGET... DIRECTORY\n");
}

int main(int argc, char *argv[])
{
    int symbolic = 0;
    int a = 1;
    while (a < argc && argv[a][0] == '-') {
        if (strcmp(argv[a], "-s") == 0)
            symbolic = 1;
        else if (strcmp(argv[a], "-f") == 0) {
            /* -f: remove existing target (handled below) */
        } else if (strcmp(argv[a], "-n") == 0) {
            /* -n: no-deref, ignore */
        } else {
            usage();
            return 1;
        }
        a++;
    }

    if (argc - a < 2) {
        usage();
        return 1;
    }

    int n = argc - a;
    const char **srcs = (const char **)&argv[a];
    const char *last = srcs[n - 1];

    /* If the last argument is a directory, link into it. */
    struct stat st;
    int dest_is_dir = (stat(last, &st) == 0 && (st.st_type == DT_DIR));

    if (dest_is_dir) {
        for (int i = 0; i < n - 1; i++) {
            char full[300];
            const char *base = strrchr(srcs[i], '/');
            base = base ? base + 1 : srcs[i];
            snprintf(full, sizeof(full), "%s/%s", last, base);
            unlink(full);   /* -f semantics: replace existing */
            if ((symbolic ? symlink(srcs[i], full) : link(srcs[i], full)) < 0) {
                fprintf(stderr, "ln: %s: cannot create link\n", full);
                return 1;
            }
        }
        return 0;
    }

    /* Single target -> link name */
    if (n > 2) {
        fprintf(stderr, "ln: %s: not a directory\n", last);
        return 1;
    }
    unlink(last);   /* -f semantics */
    if ((symbolic ? symlink(srcs[0], last) : link(srcs[0], last)) < 0) {
        fprintf(stderr, "ln: %s: cannot create link\n", last);
        return 1;
    }
    return 0;
}
