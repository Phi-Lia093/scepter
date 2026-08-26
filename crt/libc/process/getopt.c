/* ============================================================================
 * getopt - minimal POSIX option parsing.
 * ============================================================================ */

#include <unistd.h>
#include <string.h>

char *optarg = NULL;
int optind = 1;
int opterr = 1;
int optopt = 0;

int getopt(int argc, char *const argv[], const char *optstring)
{
    static int pos = 0;

    if (optind >= argc || argv[optind] == NULL)
        return -1;

    const char *arg = argv[optind];

    if (pos == 0) {
        if (arg[0] != '-' || arg[1] == '\0')
            return -1;               /* non-option: stop */
        if (strcmp(arg, "--") == 0) {
            optind++;
            return -1;
        }
        pos = 1;
    }

    char c = arg[pos];
    optopt = (int)c;

    const char *p = strchr(optstring, c);
    if (!p) {
        if (pos > 1) {
            arg = argv[optind++];
            pos = 0;
        } else {
            pos = 0;
            optind++;
        }
        return (int)'?';
    }

    if (p[1] == ':') {
        /* Option requires an argument. */
        if (arg[pos + 1] != '\0') {
            optarg = (char *)&arg[pos + 1];
            optind++;
            pos = 0;
            return (int)c;
        }
        if (optind + 1 < argc) {
            optarg = argv[optind + 1];
            optind += 2;
            pos = 0;
            return (int)c;
        }
        pos = 0;
        optind++;
        return optstring[0] == ':' ? (int)':' : (int)'?';
    }

    if (arg[pos + 1] != '\0') {
        pos++;
    } else {
        pos = 0;
        optind++;
    }
    return (int)c;
}
