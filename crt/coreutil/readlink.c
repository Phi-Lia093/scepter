/* readlink - print the target of a symbolic link */
#include "stdio.h"
#include "unistd.h"

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "usage: readlink FILE\n");
        return 1;
    }

    char buf[512];
    ssize_t n = readlink(argv[1], buf, sizeof(buf) - 1);
    if (n < 0) {
        fprintf(stderr, "readlink: %s: not a symbolic link\n", argv[1]);
        return 1;
    }
    buf[n] = '\0';
    printf("%s\n", buf);
    return 0;
}
