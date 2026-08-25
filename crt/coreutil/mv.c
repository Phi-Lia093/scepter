/* mv - rename a file */
#include "stdio.h"
#include "unistd.h"

int main(int argc, char *argv[])
{
    if (argc < 3) {
        fprintf(stderr, "usage: mv src dst\n");
        return 1;
    }
    if (rename(argv[1], argv[2]) < 0) {
        fprintf(stderr, "mv: rename failed\n");
        return 1;
    }
    return 0;
}
