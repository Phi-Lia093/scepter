#ifndef _DIRENT_H
#define _DIRENT_H

#include <stdint.h>
#include <sys/types.h>

/* Directory entry as returned by the kernel getdents() syscall.
 * Layout must match kernel fs/fs.h dirent_t. */

struct dirent {
    char     d_name[256];   /* null-terminated name (not full path) */
    uint32_t d_ino;         /* inode number (0 if unsupported)      */
    uint8_t  d_type;        /* DT_REG, DT_DIR, ...                 */
};

#define DT_UNKNOWN 0
#define DT_REG     1
#define DT_DIR     2
#define DT_CHRDEV  3
#define DT_BLKDEV  4
#define DT_SYMLINK 5

/* Raw syscall wrapper (one entry per call, "count" entries max). */
int getdents(int fd, struct dirent *buf, unsigned int count);

/* Simple buffered directory stream */
typedef struct {
    int              fd;
    struct dirent    cur;   /* single-entry buffer (kept small so it never
                             * spans an unmapped demand-paged heap page) */
} DIR;

DIR *opendir(const char *path);
struct dirent *readdir(DIR *dir);
int closedir(DIR *dir);

#endif /* _DIRENT_H */
