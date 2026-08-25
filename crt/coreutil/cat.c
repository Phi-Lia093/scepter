/* cat - concatenate files to stdout */
#include "stdio.h"
#include "unistd.h"
#include "fcntl.h"
#include "string.h"

int main(int argc, char *argv[])
{
    if (argc < 2) {
        /* stdin -> stdout; read 1 char at a time on a tty */
        if (isatty(STDIN_FILENO)) {
            char c;
            while (read(STDIN_FILENO, &c, 1) == 1)
                write(STDOUT_FILENO, &c, 1);
        } else {
            char buf[512];
            int n;
            while ((n = read(STDIN_FILENO, buf, sizeof buf)) > 0)
                write(STDOUT_FILENO, buf, n);
        }
        return 0;
    }

    for (int i = 1; i < argc; i++) {
        int fd = open(argv[i], O_RDONLY);
        if (fd < 0) {
            fprintf(stderr, "cat: %s: no such file\n", argv[i]);
            continue;
        }
        char buf[512];
        int n;
        while ((n = read(fd, buf, sizeof buf)) > 0)
            write(STDOUT_FILENO, buf, n);
        close(fd);
    }
    return 0;
}
