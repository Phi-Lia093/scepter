/* stat - display file or filesystem status */
#include <stdio.h>
#include <sys/stat.h>
#include <time.h>
#include <string.h>

static const char *type_name(uint8_t t)
{
    switch (t) {
        case DT_REG:    return "regular file";
        case DT_DIR:    return "directory";
        case DT_CHRDEV: return "character device";
        case DT_BLKDEV: return "block device";
        case DT_SYMLINK:return "symbolic link";
        default:        return "unknown";
    }
}

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "usage: stat FILE...\n");
        return 1;
    }

    for (int a = 1; a < argc; a++) {
        struct stat st;
        if (stat(argv[a], &st) < 0) {
            fprintf(stderr, "stat: %s: no such file\n", argv[a]);
            continue;
        }

        printf("  File: %s\n", argv[a]);
        printf("  Size: %u\t\tType: %s\n", st.st_size, type_name(st.st_type));
        printf("  Inode: %u\n", st.st_ino);
        if (st.st_mtime) {
            time_t t = (time_t)st.st_mtime;
            char buf[64];
            strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", localtime(&t));
            printf("  Modify: %s\n", buf);
        }
        printf("\n");
    }
    return 0;
}
