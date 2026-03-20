#include "fs/fs.h"
#include "kernel/sched.h"
#include "mm/slab.h"
#include "lib/printk.h"
#include "lib/string.h"
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
 * File Handle Management
 * ========================================================================= */

static file_handle_t *alloc_file_handle(void)
{
    file_handle_t *fh = (file_handle_t *)kalloc(sizeof(file_handle_t));
    if (!fh) return NULL;

    fh->fd           = current->next_fd++;
    fh->fs_id        = -1;
    fh->fs_private   = NULL;
    fh->file_private = NULL;
    fh->offset       = 0;
    fh->flags        = 0;

    list_add_tail(&fh->node, &current->files);
    return fh;
}

static file_handle_t *find_file_handle(int fd)
{
    file_handle_t *fh;
    list_for_each_entry(fh, &current->files, node) {
        if (fh->fd == fd) return fh;
    }
    return NULL;
}

static void free_file_handle(int fd)
{
    file_handle_t *fh;
    list_for_each_entry(fh, &current->files, node) {
        if (fh->fd == fd) {
            list_del(&fh->node);
            kfree(fh);
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
    if (!mp) { printk("[VFS] No mount point for: %s\n", abs); return -1; }

    file_handle_t *fh = alloc_file_handle();
    if (!fh) { printk("[VFS] OOM: file handle\n"); return -1; }

    int   fs_id       = mp->fs_id;
    void *file_private = NULL;

    if (fs_drivers[fs_id].ops.open) {
        if (fs_drivers[fs_id].ops.open(mp->fs_private, rel_path,
                                       flags, &file_private) != 0) {
            free_file_handle(fh->fd);
            return -1;
        }
    }

    fh->fs_id        = fs_id;
    fh->fs_private   = mp->fs_private;
    fh->file_private = file_private;
    fh->offset       = 0;
    fh->flags        = flags;
    return fh->fd;
}

int fs_close(int fd)
{
    file_handle_t *fh = find_file_handle(fd);
    if (!fh) return -1;

    if (fs_drivers[fh->fs_id].ops.close)
        fs_drivers[fh->fs_id].ops.close(fh->file_private);

    free_file_handle(fd);
    return 0;
}

int fs_read(int fd, void *buf, size_t count)
{
    if (!buf) return -1;

    file_handle_t *fh = find_file_handle(fd);
    if (!fh) return -1;
    if ((fh->flags & O_RDWR) == O_WRONLY) return -1;

    int n = -1;
    if (fs_drivers[fh->fs_id].ops.read) {
        n = fs_drivers[fh->fs_id].ops.read(fh->file_private, buf, count);
        if (n > 0) fh->offset += n;
    }
    return n;
}

int fs_write(int fd, const void *buf, size_t count)
{
    if (!buf) return -1;

    file_handle_t *fh = find_file_handle(fd);
    if (!fh) return -1;
    if ((fh->flags & O_RDWR) == O_RDONLY) return -1;

    int n = -1;
    if (fs_drivers[fh->fs_id].ops.write) {
        n = fs_drivers[fh->fs_id].ops.write(fh->file_private, buf, count);
        if (n > 0) fh->offset += n;
    }
    return n;
}

int fs_seek(int fd, int32_t offset, int whence)
{
    file_handle_t *fh = find_file_handle(fd);
    if (!fh) return -1;

    if (fs_drivers[fh->fs_id].ops.seek) {
        uint32_t new_offset = 0;
        if (fs_drivers[fh->fs_id].ops.seek(fh->file_private, offset,
                                            whence, &new_offset) != 0)
            return -1;
        fh->offset = new_offset;
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
        new_off = (int32_t)fh->offset + offset;
        if (new_off < 0) return -1;
        break;
    case SEEK_END:
        /* Need FS cooperation for SEEK_END – return error if not supported */
        return -1;
    default:
        return -1;
    }
    fh->offset = (uint32_t)new_off;
    return (int)fh->offset;
}

int fs_truncate(int fd, uint32_t length)
{
    file_handle_t *fh = find_file_handle(fd);
    if (!fh) return -1;
    if ((fh->flags & O_RDWR) == O_RDONLY) return -1;

    if (fs_drivers[fh->fs_id].ops.truncate)
        return fs_drivers[fh->fs_id].ops.truncate(fh->file_private, length);

    return -1;  /* FS driver does not support truncate */
}

int fs_readdir(int fd, dirent_t *dirent)
{
    if (!dirent) return -1;

    file_handle_t *fh = find_file_handle(fd);
    if (!fh) return -1;

    if (fs_drivers[fh->fs_id].ops.readdir)
        return fs_drivers[fh->fs_id].ops.readdir(fh->file_private, dirent);

    return -1;
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
    const char *rel_old, *rel_new;
    mount_point_t *mp_old = resolve_mount(abs_old, &rel_old);
    mount_point_t *mp_new = resolve_mount(abs_new, &rel_new);

    if (!mp_old || !mp_new || mp_old != mp_new) {
        printk("[VFS] fs_rename: cross-device rename not supported\n");
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
    if (!mp) { printk("[VFS] chdir: no mount point for %s\n", abs); return -1; }

    int fs_id = mp->fs_id;
    if (fs_drivers[fs_id].ops.stat) {
        stat_t st;
        if (fs_drivers[fs_id].ops.stat(mp->fs_private, rel, &st) != 0) {
            printk("[VFS] chdir: %s not found\n", abs);
            return -1;
        }
        if (st.type != DT_DIR) {
            printk("[VFS] chdir: %s is not a directory\n", abs);
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

    printk("[VFS] Virtual filesystem initialized\n");
}