/* seq - print a sequence of numbers: seq LAST | seq FIRST LAST | seq FIRST STEP LAST */
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[])
{
    long first = 1, step = 1, last = 0;

    if (argc == 2) {
        last = strtol(argv[1], NULL, 10);
    } else if (argc == 3) {
        first = strtol(argv[1], NULL, 10);
        last  = strtol(argv[2], NULL, 10);
    } else if (argc == 4) {
        first = strtol(argv[1], NULL, 10);
        step  = strtol(argv[2], NULL, 10);
        last  = strtol(argv[3], NULL, 10);
    } else {
        fprintf(stderr, "usage: seq [FIRST [STEP]] LAST\n");
        return 1;
    }

    if (step == 0) {
        fprintf(stderr, "seq: step must not be 0\n");
        return 1;
    }

    if (step > 0) {
        for (long i = first; i <= last; i += step)
            printf("%ld\n", i);
    } else {
        for (long i = first; i >= last; i += step)
            printf("%ld\n", i);
    }
    return 0;
}
