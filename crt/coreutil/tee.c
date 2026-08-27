/* tee - copy stdin to stdout and to every named file */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#define MAX_FILES 16

int main(int argc, char *argv[])
{
    int append = 0;
    int a = 1;
    while (a < argc && argv[a][0] == '-') {
        if (strcmp(argv[a], "-a") == 0) {
            append = 1;
            a++;
        } else if (strcmp(argv[a], "--") == 0) {
            a++;
            break;
        } else {
            fprintf(stderr, "tee: bad option %s\n", argv[a]);
            return 1;
        }
    }

    FILE files[MAX_FILES];
    int nfiles = 0;
    for (; a < argc && nfiles < MAX_FILES; a++) {
        int flags = O_WRONLY | O_CREAT | (append ? O_APPEND : O_TRUNC);
        files[nfiles] = open(argv[a], flags, 0644);
        if (files[nfiles] < 0) {
            fprintf(stderr, "tee: %s: cannot open\n", argv[a]);
            return 1;
        }
        nfiles++;
    }

    char buf[512];
    ssize_t r;
    while ((r = read(0, buf, sizeof(buf))) > 0) {
        write(1, buf, (size_t)r);
        for (int i = 0; i < nfiles; i++)
            write(files[i], buf, (size_t)r);
    }

    for (int i = 0; i < nfiles; i++)
        close(files[i]);
    return 0;
}
