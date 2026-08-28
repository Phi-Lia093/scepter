/* dmesg - print the kernel message buffer (/dev/kmsg) */
#include "stdio.h"
#include "unistd.h"
#include "fcntl.h"

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    int fd = open("/dev/kmsg", O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "dmesg: cannot open /dev/kmsg\n");
        return 1;
    }

    char buf[512];
    long n;
    /* Read all complete lines currently buffered. */
    while ((n = read(fd, buf, sizeof(buf))) > 0)
        write(STDOUT_FILENO, buf, (size_t)n);

    close(fd);
    return 0;
}
