/* printf - format and print arguments (POSIX-style) */
#include "stdio.h"
#include "string.h"
#include "stdlib.h"
#include <stdarg.h>

static void print_escaped(const char *fmt, int *count)
{
    /* Interpret \n \t \r \b \a \\ \0NNN; '\\c' ends output. */
    for (const char *p = fmt; *p; p++) {
        if (*p != '\\') {
            putchar(*p);
            (*count)++;
            continue;
        }
        p++;
        if (!*p)
            return;
        switch (*p) {
        case 'n': putchar('\n'); (*count)++; break;
        case 't': putchar('\t'); (*count)++; break;
        case 'r': putchar('\r'); (*count)++; break;
        case 'b': putchar('\b'); (*count)++; break;
        case 'a': putchar('\a'); (*count)++; break;
        case '\\': putchar('\\'); (*count)++; break;
        case 'c': return;                 /* stop output */
        case '0': {
            int v = 0, d = 0;
            while (d < 3 && p[1] >= '0' && p[1] <= '7') {
                v = v * 8 + (p[1] - '0');
                p++;
                d++;
            }
            putchar(v);
            (*count)++;
            break;
        }
        default:
            putchar(*p);
            (*count)++;
            break;
        }
    }
}

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "usage: printf FORMAT [ARG...]\n");
        return 1;
    }

    const char *fmt = argv[1];
    int argi = 2;
    int chars = 0;

    for (const char *p = fmt; *p; p++) {
        if (*p == '\\') {
            /* backslash escapes in the format (POSIX printf) */
            p++;
            if (!*p)
                break;
            switch (*p) {
            case 'n': putchar('\n'); chars++; break;
            case 't': putchar('\t'); chars++; break;
            case 'r': putchar('\r'); chars++; break;
            case 'b': putchar('\b'); chars++; break;
            case 'a': putchar('\a'); chars++; break;
            case '\\': putchar('\\'); chars++; break;
            case 'c': return 0;   /* stop output */
            default: putchar('\\'); putchar(*p); chars += 2; break;
            }
            continue;
        }
        if (*p != '%') {
            putchar(*p);
            chars++;
            continue;
        }
        p++;
        if (!*p)
            break;
        if (*p == '%') {
            putchar('%');
            chars++;
            continue;
        }
        if (*p == 'b') {
            const char *s = argi < argc ? argv[argi++] : "";
            print_escaped(s, &chars);
            continue;
        }

        /* parse flags and width */
        char tmp[64];
        int ti = 0;
        tmp[ti++] = '%';
        while (*p == '-' || *p == '+' || *p == ' ' || *p == '0' || *p == '#') {
            if (ti < 60) tmp[ti++] = *p;
            p++;
        }
        while (*p >= '0' && *p <= '9') {
            if (ti < 60) tmp[ti++] = *p;
            p++;
        }
        if (*p == '.') {
            if (ti < 60) tmp[ti++] = *p;
            p++;
            while (*p >= '0' && *p <= '9') {
                if (ti < 60) tmp[ti++] = *p;
                p++;
            }
        }
        if (*p == 'l') {
            if (ti < 60) tmp[ti++] = *p;
            p++;
        }
        if (ti < 60)
            tmp[ti++] = *p;
        tmp[ti] = '\0';

        switch (*p) {
        case 's': {
            const char *s = argi < argc ? argv[argi++] : "";
            char out[256];
            snprintf(out, sizeof(out), tmp, s);
            fputs(out, stdout);
            chars += (int)strlen(out);
            break;
        }
        case 'd':
        case 'i': {
            int v = argi < argc ? atoi(argv[argi++]) : 0;
            char out[64];
            snprintf(out, sizeof(out), tmp, v);
            fputs(out, stdout);
            chars += (int)strlen(out);
            break;
        }
        case 'u':
        case 'x':
        case 'X':
        case 'o': {
            unsigned int v = argi < argc ? (unsigned int)strtol(argv[argi++], NULL, 0) : 0;
            char out[64];
            snprintf(out, sizeof(out), tmp, v);
            fputs(out, stdout);
            chars += (int)strlen(out);
            break;
        }
        case 'c': {
            int c = argi < argc ? argv[argi++][0] : 0;
            putchar(c);
            chars++;
            break;
        }
        default:
            /* unknown specifier: emit literally */
            fputs(tmp, stdout);
            chars += ti;
            break;
        }
    }
    return 0;
}
