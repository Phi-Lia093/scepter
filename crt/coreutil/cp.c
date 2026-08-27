/* cp - copy a file */
#include "stdio.h"
#include "unistd.h"
#include "fcntl.h"

int main(int argc, char *argv[])
{
    if (argc < 3) {
        fprintf(stderr, "usage: cp src dst\n");
        return 1;
    }
    int in = open(argv[1], O_RDONLY);
    if (in < 0) {
        fprintf(stderr, "cp: %s: no such file\n", argv[1]);
        return 1;
    }
    int out = open(argv[2], O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (out < 0) {
        fprintf(stderr, "cp: %s: cannot create\n", argv[2]);
        return 1;
    }
    char buf[512];
    int n;
    while ((n = read(in, buf, sizeof buf)) > 0)
        write(out, buf, n);
    close(in);
    close(out);
    return 0;
}
