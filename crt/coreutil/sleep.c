/* sleep - delay for N seconds */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "usage: sleep seconds\n");
        return 1;
    }
    unsigned int secs = (unsigned int)atoi(argv[1]);
    sleep(secs);
    return 0;
}
