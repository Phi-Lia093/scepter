/* tr - translate or delete characters */
#include "stdio.h"
#include "unistd.h"
#include "string.h"

static int in_set(const char *set, char c)
{
    /* supports a-z style ranges and \n \t escapes */
    for (const char *p = set; *p; p++) {
        if (p[1] == '-' && p[2] && p[2] != '-') {
            if (c >= p[0] && c <= p[2])
                return 1;
            p += 2;
        } else {
            char want = *p;
            if (want == '\\' && p[1]) {
                p++;
                if (*p == 'n') want = '\n';
                else if (*p == 't') want = '\t';
                else want = *p;
            }
            if (c == want)
                return 1;
        }
    }
    return 0;
}

/* Return the char that position idx in set maps to (for translate). */
static char set_at(const char *set, int idx)
{
    const char *p = set;
    int i = 0;
    for (; *p; ) {
        if (p[1] == '-' && p[2] && p[2] != '-') {
            for (char c = p[0]; c <= p[2] && c >= p[0]; c++) {
                if (i == idx) return c;
                i++;
            }
            p += 3;
        } else {
            char want = *p;
            if (want == '\\' && p[1]) {
                p++;
                if (*p == 'n') want = '\n';
                else if (*p == 't') want = '\t';
                else want = *p;
            }
            if (i == idx) return want;
            i++;
            p++;
        }
    }
    return set[idx] ? set[idx] : '\0';
}

int main(int argc, char *argv[])
{
    int delete = 0, squeeze = 0;
    const char *set1 = NULL, *set2 = NULL;
    int a = 1;
    while (a < argc && argv[a][0] == '-') {
        for (const char *f = argv[a] + 1; *f; f++) {
            if (*f == 'd') delete = 1;
            else if (*f == 's') squeeze = 1;
            else {
                fprintf(stderr, "tr: unknown option: -%c\n", *f);
                return 1;
            }
        }
        a++;
    }
    if (a < argc) set1 = argv[a++];
    if (a < argc) set2 = argv[a++];
    (void)squeeze;   /* -s accepted but squeeze is a no-op for now */

    char c;
    while (read(STDIN_FILENO, &c, 1) == 1) {
        if (delete) {
            if (!in_set(set1, c))
                putchar(c);
            continue;
        }
        if (set1 && set2 && in_set(set1, c)) {
            /* translate to corresponding char in set2 */
            int idx = 0;
            const char *p = set1;
            /* find index of c in set1 */
            for (; *p; ) {
                if (p[1] == '-' && p[2] && p[2] != '-') {
                    if (c >= p[0] && c <= p[2]) {
                        idx += (int)(c - p[0]);
                        break;
                    }
                    idx += (int)(p[2] - p[0] + 1);
                    p += 3;
                } else {
                    char want = *p;
                    if (want == '\\' && p[1]) {
                        p++;
                        if (*p == 'n') want = '\n';
                        else if (*p == 't') want = '\t';
                        else want = *p;
                    }
                    if (c == want)
                        break;
                    idx++;
                    p++;
                }
            }
            c = set_at(set2, idx);
        }
        putchar(c);
    }
    return 0;
}
