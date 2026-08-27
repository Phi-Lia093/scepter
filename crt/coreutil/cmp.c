/* cmp - compare two files byte by byte */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

int main(int argc, char *argv[])
{
    if (argc < 3) {
        fprintf(stderr, "usage: cmp FILE1 FILE2\n");
        return 2;
    }

    int f1 = open(argv[1], O_RDONLY);
    int f2 = open(argv[2], O_RDONLY);
    if (f1 < 0 || f2 < 0) {
        fprintf(stderr, "cmp: cannot open input file\n");
        if (f1 >= 0) close(f1);
        if (f2 >= 0) close(f2);
        return 2;
    }

    char b1[512], b2[512];
    unsigned long off = 0;
    int differ = 0;

    for (;;) {
        ssize_t r1 = read(f1, b1, sizeof(b1));
        ssize_t r2 = read(f2, b2, sizeof(b2));

        ssize_t n = r1 < r2 ? r1 : r2;
        for (ssize_t i = 0; i < n; i++) {
            if (b1[i] != b2[i]) {
                printf("%s %s differ: byte %lu, char %u %u\n",
                       argv[1], argv[2], off + (unsigned long)i,
                       (unsigned)b1[i], (unsigned)b2[i]);
                differ = 1;
                goto done;
            }
        }

        if (r1 == 0 && r2 == 0)
            break;   /* identical */
        if (r1 != r2) {
            printf("cmp: EOF on %s\n", r1 < r2 ? argv[1] : argv[2]);
            differ = 1;
            break;
        }
        off += (unsigned long)n;
    }

done:
    close(f1);
    close(f2);
    return differ ? 1 : 0;
}
