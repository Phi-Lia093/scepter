/* rmdir - remove empty directories */
#include "stdio.h"
#include "unistd.h"

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "usage: rmdir dir...\n");
        return 1;
    }
    for (int i = 1; i < argc; i++) {
        if (rmdir(argv[i]) < 0)
            fprintf(stderr, "rmdir: %s: failed\n", argv[i]);
    }
    return 0;
}
