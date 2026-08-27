#ifndef _SYS_VFS_H
#define _SYS_VFS_H

#include <stdint.h>

struct statfs {
    uint32_t f_type;      /* filesystem type magic               */
    uint32_t f_bsize;     /* optimal transfer block size (bytes) */
    uint32_t f_blocks;    /* total data blocks                   */
    uint32_t f_bfree;     /* free data blocks                    */
    uint32_t f_bavail;    /* free blocks for unprivileged        */
    uint32_t f_files;     /* total inodes                        */
    uint32_t f_ffree;     /* free inodes                         */
    uint32_t f_fsid;      /* filesystem ID                       */
    uint32_t f_namelen;   /* maximum filename length             */
    uint32_t f_frsize;    /* fragment size                       */
    uint32_t f_spare[2];
};

int statfs(const char *path, struct statfs *buf);
int fstatfs(int fd, struct statfs *buf);

#endif /* _SYS_VFS_H */
