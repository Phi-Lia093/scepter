/* echo - print arguments */
#include "stdio.h"
#include "string.h"

int main(int argc, char *argv[])
{
    int i = 1, nl = 1;
    if (argc > 1 && strcmp(argv[1], "-n") == 0) { nl = 0; i = 2; }
    int first = 1;
    for (; i < argc; i++) {
        if (!first) printf(" ");
        printf("%s", argv[i]);
        first = 0;
    }
    if (nl) printf("\n");
    return 0;
}
