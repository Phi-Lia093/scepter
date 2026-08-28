/* hostname - print or set the system hostname */
#include "stdio.h"
#include "unistd.h"
#include "fcntl.h"
#include "string.h"
#include "sys/utsname.h"

int main(int argc, char *argv[])
{
    if (argc > 1) {
        if (sethostname(argv[1], strlen(argv[1])) < 0) {
            fprintf(stderr, "hostname: cannot set hostname\n");
            return 1;
        }
        /* persist to /etc/hostname */
        int fd = open("/etc/hostname", O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd >= 0) {
            write(fd, argv[1], strlen(argv[1]));
            write(fd, "\n", 1);
            close(fd);
        }
        return 0;
    }

    struct utsname un;
    if (uname(&un) < 0) {
        fprintf(stderr, "hostname: uname failed\n");
        return 1;
    }
    printf("%s\n", un.nodename);
    return 0;
}
