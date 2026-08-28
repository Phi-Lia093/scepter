/* dirname - print the directory part of a path */
#include "stdio.h"
#include "string.h"

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "usage: dirname PATH\n");
        return 1;
    }

    const char *path = argv[1];
    size_t len = strlen(path);
    if (len == 0) {
        printf(".\n");
        return 0;
    }
    /* strip trailing slashes */
    while (len > 1 && path[len - 1] == '/')
        len--;

    /* find last slash in [0, len) */
    int slash = -1;
    for (size_t i = 0; i < len; i++) {
        if (path[i] == '/')
            slash = (int)i;
    }

    if (slash < 0) {
        printf(".\n");
    } else if (slash == 0) {
        printf("/\n");
    } else {
        printf("%.*s\n", slash, path);
    }
    return 0;
}
