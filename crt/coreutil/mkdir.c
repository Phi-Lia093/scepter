/* mkdir - create directories */
#include "stdio.h"
#include "sys/stat.h"
#include "unistd.h"

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "usage: mkdir dir...\n");
        return 1;
    }
    for (int i = 1; i < argc; i++) {
        if (mkdir(argv[i], 0777) < 0)
            fprintf(stderr, "mkdir: %s: failed\n", argv[i]);
    }
    return 0;
}
