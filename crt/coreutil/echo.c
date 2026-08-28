/* echo - print arguments */
#include "stdio.h"
#include "string.h"
#include "unistd.h"

static void print_escaped(const char *s)
{
    for (const char *p = s; *p; p++) {
        if (*p != '\\') {
            putchar(*p);
            continue;
        }
        p++;
        if (!*p)
            return;
        switch (*p) {
        case 'n': putchar('\n'); break;
        case 't': putchar('\t'); break;
        case 'r': putchar('\r'); break;
        case 'b': putchar('\b'); break;
        case 'a': putchar('\a'); break;
        case '\\': putchar('\\'); break;
        case 'c': return;   /* stop output entirely */
        case '0': {
            int v = 0, d = 0;
            while (d < 3 && p[1] >= '0' && p[1] <= '7') {
                v = v * 8 + (p[1] - '0');
                p++;
                d++;
            }
            putchar(v);
            break;
        }
        default:
            putchar(*p);
            break;
        }
    }
}

int main(int argc, char *argv[])
{
    int i = 1, nl = 1, escapes = 0;
    for (; i < argc && argv[i][0] == '-' && argv[i][1] != '\0'; i++) {
        if (strcmp(argv[i], "-n") == 0) nl = 0;
        else if (strcmp(argv[i], "-e") == 0) escapes = 1;
        else if (strcmp(argv[i], "-ne") == 0 || strcmp(argv[i], "-en") == 0) {
            nl = 0;
            escapes = 1;
        } else {
            break;
        }
    }
    int first = 1;
    for (; i < argc; i++) {
        if (!first) printf(" ");
        if (escapes) print_escaped(argv[i]);
        else printf("%s", argv[i]);
        first = 0;
    }
    if (nl) printf("\n");
    return 0;
}

