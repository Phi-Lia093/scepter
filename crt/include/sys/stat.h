#ifndef _SYS_STAT_H
#define _SYS_STAT_H

#include <stdint.h>
#include <sys/types.h>

/* ============================================================================
 * stat structure.  Field order/types MUST match kernel fs/fs.h stat_t
 * (the kernel copy_to_user's the whole struct).
 * ============================================================================ */

struct stat {
    uint32_t st_size;    /* file size in bytes (0 for directories) */
    uint8_t  st_type;    /* file type (DT_REG, DT_DIR, ...)        */
    uint32_t st_ino;     /* inode number (0 if unsupported)        */
    uint32_t st_ctime;   /* creation time                          */
    uint32_t st_mtime;   /* modification time                      */
    uint32_t st_mode;    /* permission bits (0 if unsupported)     */
    uint32_t st_uid;     /* owner user id (0 if unsupported)       */
    uint32_t st_gid;     /* owner group id (0 if unsupported)      */
};

/* Directory entry type constants (must match kernel fs/fs.h) */
#define DT_UNKNOWN 0
#define DT_REG     1
#define DT_DIR     2
#define DT_CHRDEV  3
#define DT_BLKDEV  4
#define DT_SYMLINK 5

/* File mode bits */
#define S_IFMT   0170000
#define S_IFREG  0100000
#define S_IFDIR  0040000
#define S_IFCHR  0020000
#define S_IFBLK  0060000
#define S_IFLNK  0120000
#define S_IFIFO  0010000

#define S_IRWXU  00700
#define S_IRUSR  00400
#define S_IWUSR  00200
#define S_IXUSR  00100
#define S_IRWXG  00070
#define S_IRGRP  00040
#define S_IWGRP  00020
#define S_IXGRP  00010
#define S_IRWXO  00007
#define S_IROTH  00004
#define S_IWOTH  00002
#define S_IXOTH  00001

#define S_ISREG(m)  (((m) & S_IFMT) == S_IFREG)
#define S_ISDIR(m)  (((m) & S_IFMT) == S_IFDIR)
#define S_ISCHR(m)  (((m) & S_IFMT) == S_IFCHR)
#define S_ISBLK(m)  (((m) & S_IFMT) == S_IFBLK)
#define S_ISLNK(m)  (((m) & S_IFMT) == S_IFLNK)
#define S_ISFIFO(m) (((m) & S_IFMT) == S_IFIFO)

int stat(const char *path, struct stat *buf);
int fstat(int fd, struct stat *buf);
int lstat(const char *path, struct stat *buf);
int fchmod(int fd, mode_t mode);
mode_t umask(mode_t mask);
int mknod(const char *path, mode_t mode, int dev);

static inline int mkfifo(const char *path, mode_t mode)
{
    return mknod(path, S_IFIFO | mode, 0);
}

#endif /* _SYS_STAT_H */


/* ---- ownership ---- */
int chown(const char *path, uid_t owner, gid_t group);
int lchown(const char *path, uid_t owner, gid_t group);
int fchown(int fd, uid_t owner, gid_t group);
