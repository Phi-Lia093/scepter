/* which - locate a command in $PATH */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "usage: which command...\n");
        return 1;
    }

    const char *path = getenv("PATH");
    if (!path)
        path = "/bin";

    int found = 0;
    for (int a = 1; a < argc; a++) {
        /* If it contains a slash, check directly. */
        if (strchr(argv[a], '/')) {
            if (access(argv[a], 0) == 0) {
                printf("%s\n", argv[a]);
                found = 1;
            }
            continue;
        }

        char buf[256];
        const char *p = path;
        while (*p) {
            const char *end = strchr(p, ':');
            size_t len = end ? (size_t)(end - p) : strlen(p);

            if (len > 0 && len + 1 + strlen(argv[a]) + 1 < sizeof(buf)) {
                strncpy(buf, p, len);
                buf[len] = '\0';
                if (buf[len - 1] != '/')
                    strcat(buf, "/");
                strcat(buf, argv[a]);
                if (access(buf, 0) == 0) {
                    printf("%s\n", buf);
                    found = 1;
                    break;
                }
            }
            if (!end) break;
            p = end + 1;
        }
    }
    return found ? 0 : 1;
}
