/* chmod - change file permission bits */
#include "stdio.h"
#include "unistd.h"
#include "sys/stat.h"
#include <string.h>

/* Apply one symbolic mode clause (e.g. "u+x", "go-w", "a=r") to *mode.
 * Returns 0 on success, -1 on parse error. */
static int apply_symbolic(mode_t *mode, const char *spec)
{
    mode_t who = 0;
    const char *p = spec;

    /* who part */
    if (*p == 'a' || *p == 'u' || *p == 'g' || *p == 'o') {
        for (; *p == 'a' || *p == 'u' || *p == 'g' || *p == 'o'; p++) {
            if (*p == 'a') who |= 00777;
            if (*p == 'u') who |= 00700;
            if (*p == 'g') who |= 00070;
            if (*p == 'o') who |= 00007;
        }
    } else {
        who = 00777;   /* default to 'a' */
    }

    /* operator + perms, possibly repeated */
    while (*p) {
        int op = *p;
        if (op != '+' && op != '-' && op != '=')
            return -1;
        p++;

        mode_t bits = 0;
        int any = 0;
        while (*p == 'r' || *p == 'w' || *p == 'x' || *p == 's' || *p == 't') {
            if (*p == 'r') bits |= 00444;
            if (*p == 'w') bits |= 00222;
            if (*p == 'x') bits |= 00111;
            if (*p == 's') bits |= 06000;
            if (*p == 't') bits |= 01000;
            p++;
            any = 1;
        }
        if (!any)
            return -1;

        if (op == '+')
            *mode |= (bits & who);
        else if (op == '-')
            *mode &= ~(bits & who);
        else {   /* '=': clear who bits, then set */
            *mode &= ~who;
            *mode |= (bits & who);
        }
    }
    return 0;
}

int main(int argc, char *argv[])
{
    int recursive = 0;
    int a = 1;
    if (argc > 1 && strcmp(argv[1], "-R") == 0) {
        recursive = 1;
        a = 2;
    }
    if (argc - a < 2) {
        fprintf(stderr, "usage: chmod [-R] MODE FILE...\n");
        return 1;
    }

    const char *modestr = argv[a];

    /* Parse mode: octal (e.g. 755) or symbolic (e.g. u+x,go-w) */
    int is_octal = 1;
    for (const char *q = modestr; *q; q++) {
        if (*q < '0' || *q > '7') { is_octal = 0; break; }
    }

    int ret = 0;
    for (int i = a + 1; i < argc; i++) {
        mode_t m = 0;
        if (is_octal) {
            m = (mode_t)0;
            for (const char *q = modestr; *q; q++)
                m = (m << 3) | (mode_t)(*q - '0');
        } else {
            struct stat st;
            if (stat(argv[i], &st) < 0) {
                fprintf(stderr, "chmod: %s: no such file\n", argv[i]);
                ret = 1;
                continue;
            }
            m = st.st_mode;
            if (apply_symbolic(&m, modestr) < 0) {
                fprintf(stderr, "chmod: invalid mode: %s\n", modestr);
                return 1;
            }
        }
        if (chmod(argv[i], m) < 0) {
            fprintf(stderr, "chmod: %s: failed\n", argv[i]);
            ret = 1;
        }
        (void)recursive;
    }
    return ret;
}
