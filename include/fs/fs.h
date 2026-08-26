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
#define O_ACCMODE    0x0003
#define O_CREAT      0x0100
#define O_APPEND     0x0200
#define O_DIRECTORY  0x0400   /* hint: opening a directory */
#define O_TRUNC      0x0800   /* truncate file on open */
#define O_NONBLOCK   0x1000   /* don't block on reads/writes when no data */
#define O_CLOEXEC    0x2000   /* close-on-exec (stored per-fd, not on file) */

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

/* poll() event/revents bits (Linux/POSIX values) */
#define POLLIN     0x0001
#define POLLPRI    0x0002
#define POLLOUT    0x0004
#define POLLERR    0x0008
#define POLLHUP    0x0010
#define POLLNVAL   0x0020

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
    uint32_t uid;     /* owner user id (0 if unsupported)       */
    uint32_t gid;     /* owner group id (0 if unsupported)      */
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
    
    /* ---- I/O control ---- */
    int (*ioctl)(void *file_private, uint32_t cmd, uint32_t arg);

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
    int (*fstat)(void *file_private, stat_t *st);

    /* ---- Links / metadata ---- */
    /* link: hard-link old_path to new_path (same filesystem) */
    int (*link)(void *fs_private, const char *old_path,
                const char *new_path);
    /* symlink: create new_path as a symbolic link to target */
    int (*symlink)(void *fs_private, const char *target,
                   const char *new_path);
    /* readlink: read the target of a symbolic link into buf */
    int (*readlink)(void *fs_private, const char *path,
                    char *buf, size_t bufsize);
    /* chmod: change permission bits of path */
    int (*chmod)(void *fs_private, const char *path, uint32_t mode);
    /* fchmod: change permission bits of an open file */
    int (*fchmod)(void *file_private, uint32_t mode);
    /* mknod: create a device node (mode has S_IFCHR/S_IFBLK, dev in dev) */
    int (*mknod)(void *fs_private, const char *path, uint32_t mode,
                 uint32_t dev);
    /* utime: set access + modification times of a path */
    int (*utime)(void *fs_private, const char *path,
                 uint32_t atime, uint32_t mtime);

    /* ---- Poll (non-blocking readiness) ---- */
    /* Returns a POLLIN/POLLOUT/POLLERR/POLLHUP mask for the open file.
     * NULL means "always ready" (regular files). */
    int (*poll)(void *file_private);
} fs_ops_t;

/* -------------------------------------------------------------------------
 * Open File Description (shared between processes after fork)
 *
 * This structure represents an open file and can be shared by multiple
 * file descriptors (potentially across different processes).
 * ------------------------------------------------------------------------- */

typedef struct open_file {
    int          fs_id;         /* index into fs_drivers[]                 */
    void        *fs_private;    /* filesystem-level mount data             */
    void        *file_private;  /* file-specific data (inode, etc.)        */
    uint32_t     offset;        /* current read/write position (SHARED!)   */
    int          flags;         /* open flags (O_RDONLY, etc.)             */
    int          owner;         /* F_SETOWN owner pid (SIGIO not yet sent) */
    int          refcount;      /* number of fd_entry's referencing this   */
    struct pipe *pipe;          /* non-NULL if this is a pipe end          */
} open_file_t;

/* -------------------------------------------------------------------------
 * File Descriptor Entry (per-process)
 *
 * Allocated per open file descriptor, stored in task_struct.files list.
 * Points to a potentially-shared open_file_t.
 * ------------------------------------------------------------------------- */

typedef struct fd_entry {
    list_head_t  node;          /* embedded in task_struct.files           */
    int          fd;            /* file descriptor number                  */
    int          cloexec;       /* FD_CLOEXEC (closed on exec)             */
    open_file_t *file;          /* pointer to shared open file description */
} fd_entry_t;

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

/**
 * Create an anonymous pipe.
 * @param fds On success holds [read_end, write_end].
 * @return 0 on success, -1 on error.
 */
int fs_pipe(int fds[2]);

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

/**
 * Perform device-specific I/O control operation.
 * Returns 0 on success, device-specific value, or -1 on error.
 */
int fs_ioctl(int fd, uint32_t cmd, uint32_t arg);

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

/** Get metadata of an already-open file descriptor. */
int fs_fstat(int fd, stat_t *st);

/**
 * Check whether the calling process may access a path.
 * @param path Path to check
 * @param mask POSIX access bits: 4 = R_OK, 2 = W_OK, 1 = X_OK, 0 = F_OK
 * @return 0 on success (or the file does not exist: -ENOENT), -EACCES if
 *         the current euid/egid lacks the requested permissions, 0 for
 *         files with no known permission bits (mode==0), and always 0 for
 *         uid 0 (root bypasses permission checks).
 */
int fs_access_perm(const char *path, int mask);

/* -------------------------------------------------------------------------
 * Extended file operations (Phase B)
 * ------------------------------------------------------------------------- */

/** Duplicate oldfd to the lowest free fd >= minfd. Returns new fd. */
int fs_dup_min(int oldfd, int minfd, int cloexec);

/** Duplicate oldfd onto newfd (closing newfd first). Returns newfd. */
int fs_dup2_fd(int oldfd, int newfd, int cloexec);

/** Close every fd with FD_CLOEXEC set (called from exec). */
void fs_close_on_exec(void);

/** Non-blocking readiness poll for an fd. Returns POLL* mask. */
int fs_poll(int fd);

/** 1 if fd is a valid, open file descriptor. */
int fs_fd_valid(int fd);

/** Return 0/1 for the FD_CLOEXEC flag, or -1 if fd is invalid. */
int fs_get_cloexec(int fd);

/** Set/clear the FD_CLOEXEC flag. Returns 0 or -1. */
int fs_set_cloexec(int fd, int on);

/** Read at an explicit offset without changing the file offset. */
int fs_pread(int fd, void *buf, size_t count, uint32_t offset);

/** Write at an explicit offset without changing the file offset. */
int fs_pwrite(int fd, const void *buf, size_t count, uint32_t offset);

/** Hard-link old_path to new_path. */
int fs_link(const char *old_path, const char *new_path);

/** Create new_path as a symlink to target. */
int fs_symlink(const char *target, const char *new_path);

/** Read the target of a symlink. Returns length, or -1. */
int fs_readlink(const char *path, char *buf, size_t bufsize);

/** Change permission bits of path. */
int fs_chmod(const char *path, uint32_t mode);

/** Change permission bits of an open fd. */
int fs_fchmod(int fd, uint32_t mode);

/** Create a device node. */
int fs_mknod(const char *path, uint32_t mode, uint32_t dev);

/** Set access + modification times of a path. */
int fs_utime(const char *path, uint32_t atime, uint32_t mtime);

/**
 * Wake all processes blocked in select()/poll() so they re-check fd
 * readiness.  Called by pipe I/O, the keyboard IRQ, and the PIT tick.
 */
void vfs_poll_wakeup(void);

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