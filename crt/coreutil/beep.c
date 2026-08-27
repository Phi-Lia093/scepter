/* beep - play a tone on the PC speaker via /dev/pcspk
 *
 *   beep [freq] [duration_ms]
 *   beep -f freq -d duration_ms
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>

int main(int argc, char *argv[])
{
    unsigned int freq = 880;
    unsigned int ms   = 200;

    int a = 1;
    while (a < argc) {
        if (strcmp(argv[a], "-f") == 0 && a + 1 < argc) {
            freq = (unsigned int)atoi(argv[++a]);
        } else if (strcmp(argv[a], "-d") == 0 && a + 1 < argc) {
            ms = (unsigned int)atoi(argv[++a]);
        } else if (argv[a][0] != '-') {
            freq = (unsigned int)atoi(argv[a]);
            if (a + 1 < argc)
                ms = (unsigned int)atoi(argv[a + 1]);
            break;
        } else {
            fprintf(stderr, "usage: beep [-f freq] [-d ms] [freq [ms]]\n");
            return 1;
        }
        a++;
    }

    int fd = open("/dev/pcspk", O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "beep: cannot open /dev/pcspk\n");
        return 1;
    }

    ioctl(fd, IOCTL_PCSPK_BEEP, freq);
    usleep(ms * 1000);
    ioctl(fd, IOCTL_PCSPK_BEEP, 0);
    close(fd);
    return 0;
}
