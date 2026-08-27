/* date - print the current date and time */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>

int main(int argc, char *argv[])
{
    /* Default format: "%a %b %e %H:%M:%S %Y" like coreutils date. */
    const char *fmt = "%a %b %e %H:%M:%S %Y";

    int a = 1;
    while (a < argc && argv[a][0] == '-') {
        if (strcmp(argv[a], "-u") == 0 || strcmp(argv[a], "--utc") == 0) {
            /* Scepter has no timezone; local == UTC anyway. */
            a++;
        } else if (strncmp(argv[a], "+", 1) == 0) {
            fmt = &argv[a][1];
            a++;
        } else {
            fprintf(stderr, "date: unknown option %s\n", argv[a]);
            return 1;
        }
    }

    time_t now = time(NULL);
    if (now == (time_t)-1) {
        fprintf(stderr, "date: cannot read clock\n");
        return 1;
    }
    struct tm *tm = localtime(&now);
    if (!tm) return 1;

    char buf[256];
    strftime(buf, sizeof(buf), fmt, tm);
    printf("%s\n", buf);
    return 0;
}
