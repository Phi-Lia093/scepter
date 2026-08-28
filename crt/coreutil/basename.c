/* basename - print the final path component */
#include "stdio.h"
#include "string.h"

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "usage: basename PATH [SUFFIX]\n");
        return 1;
    }

    const char *path = argv[1];
    /* strip trailing slashes (but not the root) */
    size_t len = strlen(path);
    while (len > 1 && path[len - 1] == '/')
        len--;

    const char *base = path;
    for (size_t i = 0; i < len; i++) {
        if (path[i] == '/')
            base = path + i + 1;
    }

    if (argc > 2 && strcmp(base + (strlen(base) - strlen(argv[2])), argv[2]) == 0) {
        size_t slen = strlen(argv[2]);
        printf("%.*s\n", (int)(strlen(base) - slen), base);
    } else {
        printf("%s\n", base);
    }
    return 0;
}
