/* file - determine file type by magic bytes */
#include "stdio.h"
#include "string.h"
#include "unistd.h"
#include "fcntl.h"
#include "sys/stat.h"

static const char *identify(const unsigned char *b, long n)
{
    if (n >= 4 && b[0] == 0x7f && b[1] == 'E' && b[2] == 'L' && b[3] == 'F') {
        int cls = (n > 18) ? b[4] : 0;   /* 1=32-bit, 2=64-bit */
        (void)cls;
        return (cls == 2) ? "ELF 64-bit LSB" : "ELF 32-bit LSB";
    }
    if (n >= 2 && b[0] == 0x1f && b[1] == 0x8b)
        return "gzip compressed data";
    if (n >= 5 && strncmp((const char *)b, "#!/", 3) == 0)
        return "script text executable";
    if (n >= 4 && strncmp((const char *)b, "\x7f\x45\x4c\x46", 4) == 0)
        return "ELF";
    if (n >= 262 && strncmp((const char *)b + 257, "ustar", 5) == 0)
        return "POSIX tar archive";
    if (n >= 8 && strncmp((const char *)b, "\x89PNG\r\n\x1a\n", 8) == 0)
        return "PNG image data";
    if (n >= 2 && b[0] == 0xff && b[1] == 0xd8)
        return "JPEG image data";
    if (n >= 4 && strncmp((const char *)b, "PK\x03\x04", 4) == 0)
        return "Zip archive data";
    if (n >= 4 && strncmp((const char *)b, "GIF8", 4) == 0)
        return "GIF image data";
    if (n >= 4 && b[0] == 0x00 && b[1] == 0x00 && b[2] == 0x01 && b[3] == 0x00)
        return "x86 boot sector";
    if (n == 0)
        return "empty";
    return "data";
}

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "usage: file FILE...\n");
        return 1;
    }

    int ret = 0;
    for (int i = 1; i < argc; i++) {
        struct stat st;
        if (lstat(argv[i], &st) < 0) {
            printf("%s: cannot stat\n", argv[i]);
            ret = 1;
            continue;
        }
        if (st.st_type == DT_DIR) {
            printf("%s: directory\n", argv[i]);
            continue;
        }
        if (st.st_type == DT_SYMLINK) {
            char t[256];
            ssize_t n = readlink(argv[i], t, sizeof(t) - 1);
            if (n >= 0) {
                t[n] = '\0';
                printf("%s: symbolic link to %s\n", argv[i], t);
                continue;
            }
        }
        int fd = open(argv[i], O_RDONLY);
        if (fd < 0) {
            printf("%s: cannot open\n", argv[i]);
            ret = 1;
            continue;
        }
        unsigned char buf[512];
        long n = read(fd, buf, sizeof(buf));
        close(fd);
        printf("%s: %s\n", argv[i], identify(buf, n));
    }
    return ret;
}
