/* cksum - CRC32 checksum of files */
#include "stdio.h"
#include "string.h"
#include "unistd.h"
#include "fcntl.h"

static unsigned int crc_table[256];
static int table_ready = 0;

static void build_table(void)
{
    for (unsigned int i = 0; i < 256; i++) {
        unsigned int c = i;
        for (int k = 0; k < 8; k++)
            c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : (c >> 1);
        crc_table[i] = c;
    }
    table_ready = 1;
}

static unsigned long cksum_fd(int fd, unsigned long *size)
{
    if (!table_ready)
        build_table();

    unsigned int crc = 0xFFFFFFFFu;
    unsigned long len = 0;
    char buf[4096];
    long r;
    while ((r = read(fd, buf, sizeof(buf))) > 0) {
        for (long i = 0; i < r; i++)
            crc = crc_table[(crc ^ (unsigned char)buf[i]) & 0xFF] ^ (crc >> 8);
        len += (unsigned long)r;
    }
    /* append length per POSIX cksum */
    unsigned long l = len;
    while (l) {
        crc = crc_table[(crc ^ (l & 0xFF)) & 0xFF] ^ (crc >> 8);
        l >>= 8;
    }
    *size = len;
    return (unsigned long)(crc ^ 0xFFFFFFFFu);
}

int main(int argc, char *argv[])
{
    if (argc < 2) {
        unsigned long size;
        unsigned long c = cksum_fd(dup(STDIN_FILENO), &size);
        printf("%lu %lu\n", c, size);
        return 0;
    }

    int ret = 0;
    for (int i = 1; i < argc; i++) {
        int fd = open(argv[i], O_RDONLY);
        if (fd < 0) {
            fprintf(stderr, "cksum: %s: no such file\n", argv[i]);
            ret = 1;
            continue;
        }
        unsigned long size;
        unsigned long c = cksum_fd(fd, &size);
        printf("%lu %lu %s\n", c, size, argv[i]);
        close(fd);
    }
    return ret;
}
