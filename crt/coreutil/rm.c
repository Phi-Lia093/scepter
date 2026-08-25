/* rm - remove files */
#include "stdio.h"
#include "unistd.h"

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "usage: rm file...\n");
        return 1;
    }
    for (int i = 1; i < argc; i++) {
        if (unlink(argv[i]) < 0)
            fprintf(stderr, "rm: %s: failed\n", argv[i]);
    }
    return 0;
}
