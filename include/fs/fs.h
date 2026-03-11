#ifndef FS_H
#define FS_H

#include <stdint.h>
#include <stddef.h>
#include "lib/list.h"

/* =========================================================================
 * Virtual Filesystem (VFS) Abstraction Layer
 * ========================================================================= */

/* -------------------------------------------------------------------------
 * Constants
 * ------------------------------------------------------------------------- */

#define MAX_MOUNT_POINTS  16
#define MAX_PATH_LEN      256
#define MAX_FS_NAME       32

/* File open flags */
#define O_RDONLY     0x0001
#define O_WRONLY     0x0002
#define O_RDWR       0x0003
#define O_CREAT      0x0100
#define O_APPEND     0x0200
#define O_DIRECTORY  0x0400   /* hint: opening a directory */
#define O_TRUNC      0x0800   /* truncate file on open */

/* Standard file descriptors (reserved) */
#define STDIN_FD   0
#define STDOUT_FD  1
#define STDERR_FD  2

/* fs_seek whence values */
#define SEEK_SET  0   /* seek from beginning of file  */
#define SEEK_CUR  1   /* seek from current position   */
#define SEEK_END  2   /* seek from end of file        */

/* Directory entry type constants */
#define DT_UNKNOWN  0
#define DT_REG      1   /* regular file    */
#define DT_DIR      2   /* directory       */
#define DT_CHRDEV   3   /* character device */
#define DT_BLKDEV   4   /* block device    */
#define DT_SYMLINK  5   /* symbolic link   */

/* -------------------------------------------------------------------------
 * dirent – one directory entry (returned by fs_readdir)
 * ------------------------------------------------------------------------- */

typedef struct dirent {
    char     name[256];   /* null-terminated entry name (not full path) */
    uint32_t inode;       /* inode number (0 if not supported)          */
    uint8_t  type;        /* DT_REG, DT_DIR, …                         */
} dirent_t;

/* -------------------------------------------------------------------------
 * stat – file / directory metadata
 * ------------------------------------------------------------------------- */

typedef struct stat {
    uint32_t size;    /* file size in bytes (0 for directories) */
    uint8_t  type;    /* DT_REG, DT_DIR, …                     */
    uint32_t inode;   /* inode number (0 if not supported)      */
    uint32_t ctime;   /* creation time  (seconds since epoch, 0 if unknown) */
    uint32_t mtime;   /* modification time                      */
    uint32_t mode;    /* permission bits (0 if not supported)   */
} stat_t;

/* -------------------------------------------------------------------------
 * Filesystem Driver Operations
 * ------------------------------------------------------------------------- */

typedef struct fs_ops {
    /* ---- Mount / unmount ---- */
    int (*mount)(int dev_id, int part_id, void **fs_private);
    int (*unmount)(void *fs_private);

    /* ---- File open / close ---- */
    int (*open)(void *fs_private, const char *path, int flags,
                void **file_private);
    int (*close)(void *file_private);

    /* ---- File I/O ---- */
    int (*read)(void *file_private, void *buf, size_t count);
    int (*write)(void *file_private, const void *buf, size_t count);

    /* ---- Seek / truncate ---- */
    /* seek: returns new offset in *new_offset; returns 0 on success */
    int (*seek)(void *file_private, int32_t offset, int whence,
                uint32_t *new_offset);
    int (*truncate)(void *file_private, uint32_t length);

    /* ---- Directory I/O ---- */
    /* readdir: fill *dirent with next entry; return 1 if entry returned,
     *          0 at end-of-directory, -1 on error */
    int (*readdir)(void *file_private, dirent_t *dirent);

    /* ---- Path-based operations (path is relative to FS root) ---- */
    int (*mkdir)(void *fs_private, const char *path, uint32_t mode);
    int (*rmdir)(void *fs_private, const char *path);
    int (*unlink)(void *fs_private, const char *path);
    int (*rename)(void *fs_private, const char *old_path,
                  const char *new_path);
    int (*stat)(void *fs_private, const char *path, stat_t *st);
} fs_ops_t;

/* -------------------------------------------------------------------------
 * File Handle (open file descriptor)
 *
 * Allocated per open file, stored in task_struct.files linked list.
 * ------------------------------------------------------------------------- */

typedef struct file_handle {
    list_head_t  node;          /* embedded in task_struct.files           */
    int          fd;            /* file descriptor number                  */
    int          fs_id;         /* index into fs_drivers[]                 */
    void        *fs_private;    /* filesystem-level mount data             */
    void        *file_private;  /* file-specific data (inode, etc.)        */
    uint32_t     offset;        /* current read/write position             */
    int          flags;         /* open flags (O_RDONLY, etc.)             */
} file_handle_t;

/* -------------------------------------------------------------------------
 * Filesystem Registration
 * ------------------------------------------------------------------------- */

int register_filesystem(const char *fs_name, fs_ops_t *ops);

/* -------------------------------------------------------------------------
 * Mount Management
 * ------------------------------------------------------------------------- */

int fs_mount(int device_id, int partition_id, const char *fs_type,
             const char *mount_path);
int fs_unmount(const char *mount_path);

/* -------------------------------------------------------------------------
 * File Operations
 * ------------------------------------------------------------------------- */

/** Open a file or directory. Returns fd >= 3 on success, -1 on error. */
int fs_open(const char *path, int flags);

/** Close an open fd. */
int fs_close(int fd);

/** Read up to count bytes. Returns bytes read, 0 = EOF, -1 = error. */
int fs_read(int fd, void *buf, size_t count);

/** Write up to count bytes. Returns bytes written, -1 = error. */
int fs_write(int fd, const void *buf, size_t count);

/**
 * Reposition the file offset.
 * whence: SEEK_SET / SEEK_CUR / SEEK_END
 * Returns new absolute offset on success, -1 on error.
 */
int fs_seek(int fd, int32_t offset, int whence);

/**
 * Truncate/extend an open file to exactly length bytes.
 * Returns 0 on success, -1 on error.
 */
int fs_truncate(int fd, uint32_t length);

/**
 * Read the next directory entry from an open directory fd.
 * Returns 1 if an entry was returned, 0 at end-of-directory, -1 on error.
 */
int fs_readdir(int fd, dirent_t *dirent);

/* -------------------------------------------------------------------------
 * Path-Based Operations
 * ------------------------------------------------------------------------- */

/** Create a directory. mode is passed to the FS driver (0 = default). */
int fs_mkdir(const char *path, uint32_t mode);

/** Remove an empty directory. */
int fs_rmdir(const char *path);

/** Delete a file. */
int fs_unlink(const char *path);

/** Rename / move old_path to new_path (must be on the same filesystem). */
int fs_rename(const char *old_path, const char *new_path);

/** Get file / directory metadata. */
int fs_stat(const char *path, stat_t *st);

/* -------------------------------------------------------------------------
 * Working Directory (stored in task_struct.cwd)
 * ------------------------------------------------------------------------- */

/**
 * Change the current working directory.
 * The path must resolve to an existing directory.
 * Returns 0 on success, -1 on error.
 */
int fs_chdir(const char *path);

/**
 * Copy the current working directory path into buf (up to size bytes).
 * Returns buf on success, NULL on error (buffer too small, etc.).
 */
char *fs_getcwd(char *buf, size_t size);

/* -------------------------------------------------------------------------
 * Initialisation
 * ------------------------------------------------------------------------- */

void vfs_init(void);

#endif /* FS_H */