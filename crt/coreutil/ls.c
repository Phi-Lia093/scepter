/* ls - list directory contents */
#include "stdio.h"
#include "string.h"
#include "dirent.h"

int main(int argc, char *argv[])
{
    const char *dirpath = (argc > 1) ? argv[1] : ".";
    DIR *d = opendir(dirpath);
    if (!d) {
        fprintf(stderr, "ls: %s: no such directory\n", dirpath);
        return 1;
    }
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.')
            continue;   /* skip . and .. */
        if (e->d_type == DT_DIR)
            printf("%s/\n", e->d_name);
        else
            printf("%s\n", e->d_name);
    }
    closedir(d);
    return 0;
}
