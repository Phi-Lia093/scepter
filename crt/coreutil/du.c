/* du - estimate file space usage */
#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "unistd.h"
#include "dirent.h"
#include "sys/stat.h"

static int summary = 0;

static unsigned long total_blocks(const char *path)
{
    struct stat st;
    if (lstat(path, &st) < 0)
        return 0;

    unsigned long blocks = (unsigned long)((st.st_size + 1023) / 1024);

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
                blocks += total_blocks(full);
            }
            closedir(d);
        }
    }
    return blocks;
}

int main(int argc, char *argv[])
{
    int human = 0;
    int a = 1;
    while (a < argc && argv[a][0] == '-') {
        for (const char *f = argv[a] + 1; *f; f++) {
            if (*f == 's') summary = 1;
            else if (*f == 'k') human = 1024;
            else if (*f == 'h') human = -1;
        }
        a++;
    }

    if (a >= argc) {
        unsigned long b = total_blocks(".");
        if (human == -1)
            printf("%luK\t.\n", b);
        else
            printf("%lu\t.\n", b);
        return 0;
    }

    int ret = 0;
    for (; a < argc; a++) {
        struct stat st;
        if (stat(argv[a], &st) < 0) {
            fprintf(stderr, "du: %s: no such file\n", argv[a]);
            ret = 1;
            continue;
        }
        if (summary) {
            unsigned long b = total_blocks(argv[a]);
            if (human == -1)
                printf("%luK\t%s\n", b, argv[a]);
            else
                printf("%lu\t%s\n", b, argv[a]);
        } else if (st.st_type == DT_DIR) {
            /* print each subdirectory line recursively (simple) */
            printf("%lu\t%s\n", total_blocks(argv[a]), argv[a]);
        } else {
            printf("%lu\t%s\n", (unsigned long)((st.st_size + 1023) / 1024),
                   argv[a]);
        }
    }
    return ret;
}
