#include "fs/fs.h"
#include "fs/pipe.h"
#include "kernel/sched.h"
#include "mm/slab.h"
#include "lib/printk.h"
#include "lib/string.h"
#include "errno.h"
#include <stdint.h>
#include <stddef.h>

/* =========================================================================
 * Internal Structures
 * ========================================================================= */

typedef struct {
    char      mount_path[MAX_PATH_LEN];
    int       fs_id;
    int       device_id;
    int       partition_id;
    void     *fs_private;
    int       in_use;
} mount_point_t;

typedef struct {
    char     fs_name[MAX_FS_NAME];
    fs_ops_t ops;
    int      in_use;
} fs_driver_t;

/* =========================================================================
 * Global State
 * ========================================================================= */

static mount_point_t mount_table[MAX_MOUNT_POINTS];
static fs_driver_t   fs_drivers[MAX_MOUNT_POINTS];

/* Wait queue for select()/poll(): any fd-state change (pipe I/O, keyboard
 * IRQ, PIT tick) wakes it so selectors re-check readiness. */
wait_queue_head_t select_wq;

/* =========================================================================
 * Path Resolution
 *
 * resolve_path: build an absolute, normalised path from cwd + input.
 *   - If input starts with '/' use it directly.
 *   - Otherwise prepend current->cwd.
 *   - Collapse "." and ".." components.
 *   - Result is written into out[0..size-1] (NUL-terminated).
 *   - Returns 0 on success, -1 if the result exceeds size.
 * ========================================================================= */

static int resolve_path(const char *input, char *out, size_t size)
{
    char tmp[MAX_PATH_LEN];
    int  tlen = 0;

    /* Build raw absolute path in tmp */
    if (input[0] == '/') {
        /* Already absolute */
        tlen = strlen(input);
        if (tlen >= MAX_PATH_LEN) return -1;
        strcpy(tmp, input);
    } else {
        /* Relative – prepend cwd */
        int cwdlen = strlen(current->cwd);
        int inlen  = strlen(input);
        if (cwdlen + 1 + inlen + 1 >= MAX_PATH_LEN) return -1;

        strcpy(tmp, current->cwd);
        tlen = cwdlen;
        /* Ensure separator */
        if (tlen > 0 && tmp[tlen - 1] != '/') {
            tmp[tlen++] = '/';
            tmp[tlen]   = '\0';
        }
        strcpy(tmp + tlen, input);
        tlen += inlen;
    }

    /* Normalise: process components into out using a simple stack approach.
     * We re-use out as the output buffer, writing component by component. */
    char *dst = out;
    char *end = out + size - 1;  /* reserve space for NUL */
    const char *p = tmp;

    /* Always start with '/' */
    if (dst >= end) return -1;
    *dst++ = '/';

    /* Track output stack (each component starts at a saved position) */
    /* Simple in-place approach: scan components, handle . and .. */
    while (*p) {
        /* Skip slashes */
        while (*p == '/') p++;
        if (!*p) break;

        /* Find end of component */
        const char *comp_start = p;
        while (*p && *p != '/') p++;
        int comp_len = (int)(p - comp_start);

        if (comp_len == 1 && comp_start[0] == '.') {
            /* Current dir – skip */
            continue;
        }

        if (comp_len == 2 && comp_start[0] == '.' && comp_start[1] == '.') {
            /* Parent dir – remove last component from out */
            /* Walk back past any trailing slash */
            if (dst > out + 1) {
                dst--;  /* step over the '/' we added */
                /* Find start of previous component */
                while (dst > out + 1 && *(dst - 1) != '/')
                    dst--;
            }
            continue;
        }

        /* Regular component – append "/component" (the leading '/' is
         * already in out from initialisation or a previous component) */
        if (dst > out + 1) {
            /* Add separator (not needed for first component after root '/') */
            if (*(dst - 1) != '/') {
                if (dst >= end) return -1;
                *dst++ = '/';
            }
        }
        if (dst + comp_len > end) return -1;
        memcpy(dst, comp_start, comp_len);
        dst += comp_len;
    }

    *dst = '\0';

    /* Ensure at minimum "/" */
    if (dst == out) {
        if (size < 2) return -1;
        out[0] = '/';
        out[1] = '\0';
    }

    return 0;
}

/* =========================================================================
 * Open File Management (with reference counting)
 * ========================================================================= */

static open_file_t *alloc_open_file(int fs_id, void *fs_priv, 
                                     void *file_priv, int flags)
{
    open_file_t *file = (open_file_t *)kalloc(sizeof(open_file_t));
    if (!file) return NULL;

    file->fs_id        = fs_id;
    file->fs_private   = fs_priv;
    file->file_private = file_priv;
    file->offset       = 0;
    file->flags        = flags;
    file->owner        = 0;
    file->refcount     = 1;
    file->pipe         = NULL;   /* not a pipe end unless fs_pipe sets it */

    return file;
}

static void open_file_put(open_file_t *file)
{
    if (!file) return;

    file->refcount--;
    if (file->refcount == 0) {
        /* Last reference - actually close the file */
        if (file->pipe) {
            /* Pipe end: release our side; pipe frees itself when both
             * ends are gone. */
            if (file->flags & O_WRONLY)
                pipe_write_release(file->pipe);
            else
                pipe_read_release(file->pipe);
        } else if (fs_drivers[file->fs_id].ops.close) {
            fs_drivers[file->fs_id].ops.close(file->file_private);
        }
        kfree(file);
    }
}

/* =========================================================================
 * File Descriptor Entry Management
 * ========================================================================= */

static fd_entry_t *alloc_fd_entry(open_file_t *file)
{
    fd_entry_t *fde = (fd_entry_t *)kalloc(sizeof(fd_entry_t));
    if (!fde) return NULL;

    fde->fd      = current->next_fd++;
    fde->file    = file;
    fde->cloexec = 0;

    list_add_tail(&fde->node, &current->files);
    return fde;
}

static fd_entry_t *find_fd_entry(int fd)
{
    fd_entry_t *fde;
    list_for_each_entry(fde, &current->files, node) {
        if (fde->fd == fd) return fde;
    }
    return NULL;
}

static void free_fd_entry(int fd)
{
    fd_entry_t *fde;
    list_for_each_entry(fde, &current->files, node) {
        if (fde->fd == fd) {
            list_del(&fde->node);
            kfree(fde);
            return;
        }
    }
}

/* =========================================================================
 * Mount Point Helpers
 * ========================================================================= */

static int find_fs_driver(const char *fs_name)
{
    for (int i = 0; i < MAX_MOUNT_POINTS; i++) {
        if (fs_drivers[i].in_use &&
            strcmp(fs_drivers[i].fs_name, fs_name) == 0)
            return i;
    }
    return -1;
}

/* Find best (longest-prefix) mount point for abs_path.
 * Sets *match_len to the length of the matched prefix.
 * Returns NULL if no mount point matches. */
static mount_point_t *find_mount_point(const char *abs_path, int *match_len)
{
    mount_point_t *best = NULL;
    int            best_len = 0;

    for (int i = 0; i < MAX_MOUNT_POINTS; i++) {
        if (!mount_table[i].in_use) continue;

        int mlen = strlen(mount_table[i].mount_path);

        /* Root "/" matches everything */
        if (mlen == 1 && mount_table[i].mount_path[0] == '/') {
            if (mlen > best_len) { best_len = mlen; best = &mount_table[i]; }
            continue;
        }

        if (strncmp(abs_path, mount_table[i].mount_path, mlen) == 0) {
            if (abs_path[mlen] == '\0' || abs_path[mlen] == '/') {
                if (mlen > best_len) { best_len = mlen; best = &mount_table[i]; }
            }
        }
    }

    if (match_len) *match_len = best_len;
    return best;
}

/* Given an absolute path, resolve its mount point and compute the
 * path relative to that mount point's root.
 * rel_path points into abs_path (no copy needed). */
static mount_point_t *resolve_mount(const char *abs_path,
                                    const char **rel_path)
{
    int match_len = 0;
    mount_point_t *mp = find_mount_point(abs_path, &match_len);
    if (!mp) return NULL;

    const char *rp = abs_path + match_len;
    if (*rp == '\0') rp = "/";
    *rel_path = rp;
    return mp;
}

/* =========================================================================
 * Filesystem Registration
 * ========================================================================= */

int register_filesystem(const char *fs_name, fs_ops_t *ops)
{
    if (!fs_name || !ops) return -1;

    for (int i = 0; i < MAX_MOUNT_POINTS; i++) {
        if (!fs_drivers[i].in_use) {
            strcpy(fs_drivers[i].fs_name, fs_name);
            fs_drivers[i].ops    = *ops;
            fs_drivers[i].in_use = 1;
            return i;
        }
    }
    return -1;
}

/* =========================================================================
 * Mount Management
 * ========================================================================= */

int fs_mount(int device_id, int partition_id,
             const char *fs_type, const char *mount_path)
{
    if (!fs_type || !mount_path) return -1;

    int fs_id = find_fs_driver(fs_type);
    if (fs_id < 0) {
        printk("[VFS] Unknown filesystem type: %s\n", fs_type);
        return -1;
    }

    int slot = -1;
    for (int i = 0; i < MAX_MOUNT_POINTS; i++) {
        if (!mount_table[i].in_use) { slot = i; break; }
    }
    if (slot < 0) { printk("[VFS] No free mount slots\n"); return -1; }

    void *fs_private = NULL;
    if (fs_drivers[fs_id].ops.mount) {
        if (fs_drivers[fs_id].ops.mount(device_id, partition_id,
                                        &fs_private) != 0) {
            printk("[VFS] Filesystem mount failed\n");
            return -1;
        }
    }

    strcpy(mount_table[slot].mount_path, mount_path);
    mount_table[slot].fs_id        = fs_id;
    mount_table[slot].device_id    = device_id;
    mount_table[slot].partition_id = partition_id;
    mount_table[slot].fs_private   = fs_private;
    mount_table[slot].in_use       = 1;

    printk("[VFS] Mounted %s (dev %d, part %d) at %s\n",
           fs_type, device_id, partition_id, mount_path);
    return 0;
}

int fs_unmount(const char *mount_path)
{
    if (!mount_path) return -1;

    for (int i = 0; i < MAX_MOUNT_POINTS; i++) {
        if (mount_table[i].in_use &&
            strcmp(mount_table[i].mount_path, mount_path) == 0)
        {
            int fs_id = mount_table[i].fs_id;
            if (fs_drivers[fs_id].ops.unmount)
                fs_drivers[fs_id].ops.unmount(mount_table[i].fs_private);

            mount_table[i].in_use     = 0;
            mount_table[i].fs_private = NULL;
            printk("[VFS] Unmounted %s\n", mount_path);
            return 0;
        }
    }
    return -1;
}

/* =========================================================================
 * File Operations
 * ========================================================================= */

int fs_open(const char *path, int flags)
{
    if (!path) return -1;

    char abs[MAX_PATH_LEN];
    if (resolve_path(path, abs, sizeof(abs)) != 0) return -1;

    const char *rel_path;
    mount_point_t *mp = resolve_mount(abs, &rel_path);

    int   fs_id        = mp->fs_id;
    void *file_private = NULL;

    /* Open the file via filesystem driver */
    if (fs_drivers[fs_id].ops.open) {
        if (fs_drivers[fs_id].ops.open(mp->fs_private, rel_path,
                                       flags, &file_private) != 0) {
            return -1;
        }
    }

    /* Create shared open_file structure with refcount=1.
     * O_CLOEXEC is a per-fd flag, not part of the file description. */
    open_file_t *file = alloc_open_file(fs_id, mp->fs_private, 
                                         file_private, flags & ~O_CLOEXEC);
    if (!file) {
        /* Cleanup on allocation failure */
        if (fs_drivers[fs_id].ops.close) {
            fs_drivers[fs_id].ops.close(file_private);
        }
        return -1;
    }

    /* Create fd_entry pointing to the open_file */
    fd_entry_t *fde = alloc_fd_entry(file);
    if (!fde) {
        /* Cleanup on allocation failure */
        open_file_put(file);  /* Will close the file since refcount=1 */
        return -1;
    }
    fde->cloexec = (flags & O_CLOEXEC) ? 1 : 0;

    return fde->fd;
}

int fs_close(int fd)
{
    fd_entry_t *fde = find_fd_entry(fd);
    if (!fde) return -1;

    /* Decrement reference count on open_file (closes if last ref) */
    open_file_put(fde->file);

    /* Free the fd_entry */
    free_fd_entry(fd);
    return 0;
}

int fs_pipe(int fds[2])
{
    if (!fds) return -1;

    pipe_t *pipe = pipe_create();
    if (!pipe) {
        return -1;
    }

    /* Read end */
    open_file_t *rfile = alloc_open_file(-1, NULL, NULL, O_RDONLY);
    if (!rfile) { pipe_read_release(pipe); return -1; }
    rfile->pipe = pipe;

    fd_entry_t *rfde = (fd_entry_t *)kalloc(sizeof(fd_entry_t));
    if (!rfde) { kfree(rfile); pipe_read_release(pipe); return -1; }
    rfde->fd      = current->next_fd++;
    rfde->file    = rfile;
    rfde->cloexec = 0;
    list_add_tail(&rfde->node, &current->files);

    /* Write end */
    open_file_t *wfile = alloc_open_file(-1, NULL, NULL, O_WRONLY);
    if (!wfile) { fs_close(rfde->fd); pipe_write_release(pipe); return -1; }
    wfile->pipe = pipe;

    fd_entry_t *wfde = (fd_entry_t *)kalloc(sizeof(fd_entry_t));
    if (!wfde) { kfree(wfile); fs_close(rfde->fd); pipe_write_release(pipe); return -1; }
    wfde->fd      = current->next_fd++;
    wfde->file    = wfile;
    wfde->cloexec = 0;
    list_add_tail(&wfde->node, &current->files);

    fds[0] = rfde->fd;
    fds[1] = wfde->fd;
    return 0;
}

int fs_read(int fd, void *buf, size_t count)
{
    if (!buf) return -1;

    fd_entry_t *fde = find_fd_entry(fd);
    if (!fde || !fde->file) return -1;
    
    open_file_t *file = fde->file;
    if ((file->flags & O_RDWR) == O_WRONLY) return -1;

    /* Pipe read (may block until data or EOF) */
    if (file->pipe) {
        if (file->flags & O_NONBLOCK)
            return pipe_read_nonblock(file->pipe, buf, count);
        return pipe_read(file->pipe, buf, count);
    }

    int n = -1;
    if (fs_drivers[file->fs_id].ops.read) {
        n = fs_drivers[file->fs_id].ops.read(file->file_private, buf, count);
        if (n > 0) file->offset += n;  /* Shared offset updated! */
    }
    return n;
}

int fs_write(int fd, const void *buf, size_t count)
{
    if (!buf) {
        return -1;
    }

    fd_entry_t *fde = find_fd_entry(fd);
    if (!fde) {
        return -1;
    }
    
    if (!fde->file) {
        return -1;
    }
    
    open_file_t *file = fde->file;
    if ((file->flags & O_RDWR) == O_RDONLY) {
        return -1;
    }

    /* Pipe write (may block until space or EPIPE) */
    if (file->pipe) {
        if (file->flags & O_NONBLOCK)
            return pipe_write_nonblock(file->pipe, buf, count);
        return pipe_write(file->pipe, buf, count);
    }

    int n = -1;
    if (fs_drivers[file->fs_id].ops.write) {
        n = fs_drivers[file->fs_id].ops.write(file->file_private, buf, count);
        if (n > 0) {
            file->offset += n;  /* Shared offset updated! */
        }
    } else {
    }
    return n;
}

int fs_seek(int fd, int32_t offset, int whence)
{
    fd_entry_t *fde = find_fd_entry(fd);
    if (!fde || !fde->file) return -1;
    
    open_file_t *file = fde->file;

    if (fs_drivers[file->fs_id].ops.seek) {
        uint32_t new_offset = 0;
        if (fs_drivers[file->fs_id].ops.seek(file->file_private, offset,
                                              whence, &new_offset) != 0)
            return -1;
        file->offset = new_offset;  /* Shared offset updated! */
        return (int)new_offset;
    }

    /* Generic fallback – only SEEK_SET / SEEK_CUR supported without FS help */
    int32_t new_off;
    switch (whence) {
    case SEEK_SET:
        if (offset < 0) return -1;
        new_off = offset;
        break;
    case SEEK_CUR:
        new_off = (int32_t)file->offset + offset;
        if (new_off < 0) return -1;
        break;
    case SEEK_END:
        /* Need FS cooperation for SEEK_END – return error if not supported */
        return -1;
    default:
        return -1;
    }
    file->offset = (uint32_t)new_off;  /* Shared offset updated! */
    return (int)file->offset;
}

int fs_truncate(int fd, uint32_t length)
{
    fd_entry_t *fde = find_fd_entry(fd);
    if (!fde || !fde->file) return -1;
    
    open_file_t *file = fde->file;
    if ((file->flags & O_RDWR) == O_RDONLY) return -1;

    if (fs_drivers[file->fs_id].ops.truncate)
        return fs_drivers[file->fs_id].ops.truncate(file->file_private, length);

    return -1;  /* FS driver does not support truncate */
}

int fs_readdir(int fd, dirent_t *dirent)
{
    if (!dirent) return -1;

    fd_entry_t *fde = find_fd_entry(fd);
    if (!fde || !fde->file) return -1;
    
    open_file_t *file = fde->file;

    if (fs_drivers[file->fs_id].ops.readdir)
        return fs_drivers[file->fs_id].ops.readdir(file->file_private, dirent);

    return -1;
}

int fs_ioctl(int fd, uint32_t cmd, uint32_t arg)
{
    fd_entry_t *fde = find_fd_entry(fd);
    if (!fde || !fde->file) return -1;
    
    open_file_t *file = fde->file;

    if (fs_drivers[file->fs_id].ops.ioctl)
        return fs_drivers[file->fs_id].ops.ioctl(file->file_private, cmd, arg);

    return -1;  /* FS driver does not support ioctl */
}

/* =========================================================================
 * Path-Based Operations
 * ========================================================================= */

int fs_mkdir(const char *path, uint32_t mode)
{
    if (!path) return -1;

    char abs[MAX_PATH_LEN];
    if (resolve_path(path, abs, sizeof(abs)) != 0) return -1;

    const char *rel;
    mount_point_t *mp = resolve_mount(abs, &rel);
    if (!mp) return -1;

    int fs_id = mp->fs_id;
    if (!fs_drivers[fs_id].ops.mkdir) return -1;
    return fs_drivers[fs_id].ops.mkdir(mp->fs_private, rel, mode);
}

int fs_rmdir(const char *path)
{
    if (!path) return -1;

    char abs[MAX_PATH_LEN];
    if (resolve_path(path, abs, sizeof(abs)) != 0) return -1;

    const char *rel;
    mount_point_t *mp = resolve_mount(abs, &rel);
    if (!mp) return -1;

    int fs_id = mp->fs_id;
    if (!fs_drivers[fs_id].ops.rmdir) return -1;
    return fs_drivers[fs_id].ops.rmdir(mp->fs_private, rel);
}

int fs_unlink(const char *path)
{
    if (!path) return -1;

    char abs[MAX_PATH_LEN];
    if (resolve_path(path, abs, sizeof(abs)) != 0) return -1;

    const char *rel;
    mount_point_t *mp = resolve_mount(abs, &rel);
    if (!mp) return -1;

    int fs_id = mp->fs_id;
    if (!fs_drivers[fs_id].ops.unlink) return -1;
    return fs_drivers[fs_id].ops.unlink(mp->fs_private, rel);
}

int fs_rename(const char *old_path, const char *new_path)
{
    if (!old_path || !new_path) return -1;

    char abs_old[MAX_PATH_LEN], abs_new[MAX_PATH_LEN];
    if (resolve_path(old_path, abs_old, sizeof(abs_old)) != 0) return -1;
    if (resolve_path(new_path, abs_new, sizeof(abs_new)) != 0) return -1;

    /* Both paths must be on the same filesystem */
    const char *rel_old = NULL , *rel_new = NULL;
    mount_point_t *mp_old = resolve_mount(abs_old, &rel_old);
    mount_point_t *mp_new = resolve_mount(abs_new, &rel_new);

    if (!mp_old || !mp_new || mp_old != mp_new) {
        return -1;
    }

    int fs_id = mp_old->fs_id;
    if (!fs_drivers[fs_id].ops.rename) return -1;
    return fs_drivers[fs_id].ops.rename(mp_old->fs_private, rel_old, rel_new);
}

int fs_stat(const char *path, stat_t *st)
{
    if (!path || !st) return -1;

    char abs[MAX_PATH_LEN];
    if (resolve_path(path, abs, sizeof(abs)) != 0) return -1;

    const char *rel;
    mount_point_t *mp = resolve_mount(abs, &rel);
    if (!mp) return -1;

    int fs_id = mp->fs_id;
    if (!fs_drivers[fs_id].ops.stat) return -1;
    return fs_drivers[fs_id].ops.stat(mp->fs_private, rel, st);
}

/**
 * fs_fstat - Get status of an open file by descriptor.
 */
int fs_fstat(int fd, stat_t *st)
{
    if (!st) return -1;

    fd_entry_t *fde = find_fd_entry(fd);
    if (!fde || !fde->file) return -1;

    open_file_t *file = fde->file;
    if (file->pipe) {
        /* Pipes have no real inode: synthesize a small stat */
        st->size   = 0;
        st->inode  = 0;
        st->ctime  = 0;
        st->mtime  = 0;
        st->mode   = 0x2000;   /* S_IFCHR-ish: not a regular file */
        st->type   = DT_CHRDEV;
        return 0;
    }

    if (!fs_drivers[file->fs_id].ops.fstat)
        return -1;
    return fs_drivers[file->fs_id].ops.fstat(file->file_private, st);
}

/* =========================================================================
 * Permission checking
 * ========================================================================= */

/**
 * fs_access_perm - Check the calling process's access to a path.
 */
int fs_access_perm(const char *path, int mask)
{
    stat_t st;
    if (fs_stat(path, &st) < 0)
        return -ENOENT;

    /* Root can access anything. */
    if (current->euid == 0)
        return 0;

    /* F_OK: existence was already confirmed by fs_stat above. */
    if (mask == 0)
        return 0;

    /* Files/devices with no known permission bits (mode == 0, e.g. devfs
     * nodes or legacy files) are accessible to everyone. */
    if ((st.mode & 0777) == 0)
        return 0;

    int shift = 0;
    if (current->euid == st.uid)
        shift = 6;             /* owner */
    else if (current->egid == st.gid)
        shift = 3;             /* group */
    else
        shift = 0;             /* other */

    if ((st.mode & 0777) & (mask << shift))
        return 0;
    return -EACCES;
}

/* =========================================================================
 * Working Directory
 * ========================================================================= */

int fs_chdir(const char *path)
{
    if (!path) return -1;

    char abs[MAX_PATH_LEN];
    if (resolve_path(path, abs, sizeof(abs)) != 0) return -1;

    /* Verify the target exists and is a directory via stat */
    const char *rel;
    mount_point_t *mp = resolve_mount(abs, &rel);

    int fs_id = mp->fs_id;
    if (fs_drivers[fs_id].ops.stat) {
        stat_t st;
        if (fs_drivers[fs_id].ops.stat(mp->fs_private, rel, &st) != 0) {
            return -1;
        }
        if (st.type != DT_DIR) {
            return -1;
        }
    }
    /* If FS doesn't implement stat, we trust the caller */

    strncpy(current->cwd, abs, MAX_PATH_LEN - 1);
    current->cwd[MAX_PATH_LEN - 1] = '\0';
    return 0;
}

char *fs_getcwd(char *buf, size_t size)
{
    if (!buf || size == 0) return NULL;

    int len = strlen(current->cwd);
    if ((size_t)(len + 1) > size) return NULL;

    strcpy(buf, current->cwd);
    return buf;
}

/* =========================================================================
 * Initialization
 * ========================================================================= */

void vfs_init(void)
{
    for (int i = 0; i < MAX_MOUNT_POINTS; i++) {
        mount_table[i].in_use     = 0;
        mount_table[i].fs_private = NULL;
    }
    for (int i = 0; i < MAX_MOUNT_POINTS; i++)
        fs_drivers[i].in_use = 0;

    init_waitqueue_head(&select_wq);

    printk("[VFS] Virtual filesystem initialized\n");
}

/* =========================================================================
 * Extended file operations (Phase B)
 * ========================================================================= */

/**
 * vfs_poll_wakeup - Wake processes blocked in select()/poll().
 * Safe from interrupt context (single-CPU, IF=0).
 */
void vfs_poll_wakeup(void)
{
    wake_up(&select_wq);
}

/**
 * fs_dup_min - Duplicate oldfd to the lowest free fd >= minfd.
 */
int fs_dup_min(int oldfd, int minfd, int cloexec)
{
    fd_entry_t *old_fde = find_fd_entry(oldfd);
    if (!old_fde || !old_fde->file)
        return -1;

    /* Find the lowest free fd >= minfd. */
    int newfd = minfd;
    int found = 0;
    while (!found) {
        found = 1;
        list_head_t *pos;
        list_for_each(pos, &current->files) {
            fd_entry_t *fde = list_entry(pos, fd_entry_t, node);
            if (fde->fd == newfd) {
                newfd++;
                found = 0;
                break;
            }
        }
    }

    fd_entry_t *new_fde = (fd_entry_t *)kalloc(sizeof(fd_entry_t));
    if (!new_fde)
        return -1;

    new_fde->fd      = newfd;
    new_fde->file    = old_fde->file;
    new_fde->cloexec = cloexec ? 1 : 0;
    old_fde->file->refcount++;

    /* Keep next_fd in sync so alloc_fd_entry() won't hand out the same fd. */
    if (newfd >= current->next_fd)
        current->next_fd = newfd + 1;

    INIT_LIST_HEAD(&new_fde->node);
    list_add_tail(&new_fde->node, &current->files);
    return newfd;
}

/**
 * fs_dup2_fd - Duplicate oldfd onto newfd (closing newfd first).
 */
int fs_dup2_fd(int oldfd, int newfd, int cloexec)
{
    if (newfd < 0)
        return -1;

    if (oldfd == newfd) {
        fd_entry_t *fde = find_fd_entry(oldfd);
        if (!fde)
            return -1;
        fde->cloexec = cloexec ? 1 : 0;
        return newfd;
    }

    fd_entry_t *old_fde = find_fd_entry(oldfd);
    if (!old_fde || !old_fde->file)
        return -1;

    /* Close newfd if it is open. */
    fd_entry_t *new_fde = find_fd_entry(newfd);
    if (new_fde) {
        open_file_put(new_fde->file);
        free_fd_entry(newfd);
    }

    new_fde = (fd_entry_t *)kalloc(sizeof(fd_entry_t));
    if (!new_fde)
        return -1;

    new_fde->fd      = newfd;
    new_fde->file    = old_fde->file;
    new_fde->cloexec = cloexec ? 1 : 0;
    old_fde->file->refcount++;

    /* Keep next_fd in sync. */
    if (newfd >= current->next_fd)
        current->next_fd = newfd + 1;

    INIT_LIST_HEAD(&new_fde->node);
    list_add_tail(&new_fde->node, &current->files);
    return newfd;
}

/**
 * fs_close_on_exec - Close every fd with FD_CLOEXEC set (exec time).
 */
void fs_close_on_exec(void)
{
    list_head_t *pos, *tmp;
    list_for_each_safe(pos, tmp, &current->files) {
        fd_entry_t *fde = list_entry(pos, fd_entry_t, node);
        if (fde->cloexec)
            fs_close(fde->fd);
    }
}

/**
 * fs_poll - Non-blocking readiness poll for an fd.
 */
int fs_poll(int fd)
{
    fd_entry_t *fde = find_fd_entry(fd);
    if (!fde || !fde->file)
        return POLLNVAL;

    open_file_t *file = fde->file;

    if (file->pipe)
        return pipe_poll(file->pipe, file->flags);

    if (fs_drivers[file->fs_id].ops.poll)
        return fs_drivers[file->fs_id].ops.poll(file->file_private);

    /* Regular files / unknown: always readable + writable. */
    return POLLIN | POLLOUT;
}

/**
 * fs_fd_valid - Check whether an fd is open.
 */
int fs_fd_valid(int fd)
{
    return find_fd_entry(fd) != NULL;
}

/**
 * fs_get_cloexec - Return 0/1 for the FD_CLOEXEC flag, or -1 if invalid.
 */
int fs_get_cloexec(int fd)
{
    fd_entry_t *fde = find_fd_entry(fd);
    if (!fde)
        return -1;
    return fde->cloexec ? 1 : 0;
}

/**
 * fs_set_cloexec - Set/clear the FD_CLOEXEC flag. Returns 0 or -1.
 */
int fs_set_cloexec(int fd, int on)
{
    fd_entry_t *fde = find_fd_entry(fd);
    if (!fde)
        return -1;
    fde->cloexec = on ? 1 : 0;
    return 0;
}

/**
 * fs_pread - Read at an explicit offset without changing the file offset.
 * Uses fs_seek so both the VFS and the FS-driver offsets stay in sync.
 */
int fs_pread(int fd, void *buf, size_t count, uint32_t offset)
{
    fd_entry_t *fde = find_fd_entry(fd);
    if (!fde || !fde->file)
        return -1;
    if (fde->file->pipe)
        return -1;   /* ESPIPE: pread is invalid on pipes */

    uint32_t saved = fde->file->offset;
    if (fs_seek(fd, (int32_t)offset, SEEK_SET) < 0)
        return -1;
    int n = fs_read(fd, buf, count);
    fs_seek(fd, (int32_t)saved, SEEK_SET);
    return n;
}

/**
 * fs_pwrite - Write at an explicit offset without changing the file offset.
 */
int fs_pwrite(int fd, const void *buf, size_t count, uint32_t offset)
{
    fd_entry_t *fde = find_fd_entry(fd);
    if (!fde || !fde->file)
        return -1;
    if (fde->file->pipe)
        return -1;   /* ESPIPE */

    uint32_t saved = fde->file->offset;
    if (fs_seek(fd, (int32_t)offset, SEEK_SET) < 0)
        return -1;
    int n = fs_write(fd, buf, count);
    fs_seek(fd, (int32_t)saved, SEEK_SET);
    return n;
}

/* ---- Links / metadata (VFS wrappers) ---- */

int fs_link(const char *old_path, const char *new_path)
{
    if (!old_path || !new_path) return -1;
    char abs_old[MAX_PATH_LEN], abs_new[MAX_PATH_LEN];
    if (resolve_path(old_path, abs_old, sizeof(abs_old)) != 0) return -1;
    if (resolve_path(new_path, abs_new, sizeof(abs_new)) != 0) return -1;

    const char *rel_old, *rel_new;
    mount_point_t *mp_old = resolve_mount(abs_old, &rel_old);
    mount_point_t *mp_new = resolve_mount(abs_new, &rel_new);
    if (!mp_old || !mp_new || mp_old->fs_id != mp_new->fs_id)
        return -1;

    if (!fs_drivers[mp_old->fs_id].ops.link)
        return -1;
    return fs_drivers[mp_old->fs_id].ops.link(mp_old->fs_private,
                                               rel_old, rel_new);
}

int fs_symlink(const char *target, const char *new_path)
{
    if (!target || !new_path) return -1;
    char abs_new[MAX_PATH_LEN];
    if (resolve_path(new_path, abs_new, sizeof(abs_new)) != 0) return -1;

    const char *rel_new;
    mount_point_t *mp = resolve_mount(abs_new, &rel_new);
    if (!mp) return -1;

    if (!fs_drivers[mp->fs_id].ops.symlink)
        return -1;
    return fs_drivers[mp->fs_id].ops.symlink(mp->fs_private, target, rel_new);
}

int fs_readlink(const char *path, char *buf, size_t bufsize)
{
    if (!path || !buf) return -1;
    char abs[MAX_PATH_LEN];
    if (resolve_path(path, abs, sizeof(abs)) != 0) return -1;

    const char *rel;
    mount_point_t *mp = resolve_mount(abs, &rel);
    if (!mp) return -1;

    if (!fs_drivers[mp->fs_id].ops.readlink)
        return -1;
    return fs_drivers[mp->fs_id].ops.readlink(mp->fs_private, rel, buf, bufsize);
}

int fs_chmod(const char *path, uint32_t mode)
{
    if (!path) return -1;
    char abs[MAX_PATH_LEN];
    if (resolve_path(path, abs, sizeof(abs)) != 0) return -1;

    const char *rel;
    mount_point_t *mp = resolve_mount(abs, &rel);
    if (!mp) return -1;

    if (!fs_drivers[mp->fs_id].ops.chmod)
        return -1;
    return fs_drivers[mp->fs_id].ops.chmod(mp->fs_private, rel, mode);
}

int fs_fchmod(int fd, uint32_t mode)
{
    fd_entry_t *fde = find_fd_entry(fd);
    if (!fde || !fde->file) return -1;

    open_file_t *file = fde->file;
    if (file->pipe) return -1;
    if (!fs_drivers[file->fs_id].ops.fchmod)
        return -1;
    return fs_drivers[file->fs_id].ops.fchmod(file->file_private, mode);
}

int fs_mknod(const char *path, uint32_t mode, uint32_t dev)
{
    if (!path) return -1;
    char abs[MAX_PATH_LEN];
    if (resolve_path(path, abs, sizeof(abs)) != 0) return -1;

    const char *rel;
    mount_point_t *mp = resolve_mount(abs, &rel);
    if (!mp) return -1;

    if (!fs_drivers[mp->fs_id].ops.mknod)
        return -1;
    return fs_drivers[mp->fs_id].ops.mknod(mp->fs_private, rel, mode, dev);
}