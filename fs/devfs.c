#include "fs/devfs.h"
#include "fs/fs.h"
#include "driver/driver.h"
#include "mm/slab.h"
#include "lib/string.h"
#include "lib/printk.h"
#include <stdint.h>
#include <stddef.h>

/* =========================================================================
 * Static Node Table
 *
 * Populated by devfs_register_device() (which may be called before mount).
 * ========================================================================= */

static devfs_node_t devfs_nodes[DEVFS_MAX_NODES];
static int          devfs_node_count = 0;

/* =========================================================================
 * Per-Open-File State
 * ========================================================================= */

typedef struct devfs_file {
    devfs_node_t *node;     /* pointer into devfs_nodes[]          */
    uint32_t      offset;   /* byte offset (block devices)         */
    int           dir_pos;  /* readdir: next node index to return  */
} devfs_file_t;

/* =========================================================================
 * Node Lookup
 * ========================================================================= */

static devfs_node_t *devfs_find_node(const char *name)
{
    for (int i = 0; i < DEVFS_MAX_NODES; i++) {
        if (devfs_nodes[i].in_use &&
            strcmp(devfs_nodes[i].name, name) == 0)
            return &devfs_nodes[i];
    }
    return NULL;
}

/* Strip a single leading '/' if present so callers can pass either
 * "hda" or "/hda" and get the same result. */
static const char *strip_leading_slash(const char *path)
{
    while (*path == '/') path++;
    return path;
}

/* =========================================================================
 * Public API – Device Registration
 * ========================================================================= */

int devfs_register_device(const char *name, uint8_t type,
                          int dev_id, int minor)
{
    if (!name || strlen(name) == 0 || strlen(name) >= 64) return -1;
    if (type != DT_BLKDEV && type != DT_CHRDEV) return -1;

    /* Reject duplicates */
    if (devfs_find_node(name)) return -1;

    /* Find a free slot */
    for (int i = 0; i < DEVFS_MAX_NODES; i++) {
        if (!devfs_nodes[i].in_use) {
            strncpy(devfs_nodes[i].name, name, 63);
            devfs_nodes[i].name[63] = '\0';
            devfs_nodes[i].type     = type;
            devfs_nodes[i].dev_id   = dev_id;
            devfs_nodes[i].minor    = minor;
            devfs_nodes[i].in_use   = 1;
            devfs_node_count++;
            return 0;
        }
    }
    return -1;   /* table full */
}

int devfs_unregister_device(const char *name)
{
    if (!name) return -1;

    for (int i = 0; i < DEVFS_MAX_NODES; i++) {
        if (devfs_nodes[i].in_use &&
            strcmp(devfs_nodes[i].name, name) == 0) {
            devfs_nodes[i].in_use = 0;
            devfs_node_count--;
            return 0;
        }
    }
    return -1;
}

/* =========================================================================
 * fs_ops_t Callbacks
 * ========================================================================= */

/* ---- mount / unmount ---- */

static int devfs_mount(int dev_id, int part_id, void **fs_private)
{
    (void)dev_id;
    (void)part_id;
    /* devfs is purely in-memory; nothing to do.
     * fs_private is unused – pass a non-NULL sentinel so VFS sees success. */
    *fs_private = (void *)1;
    printk("[devfs] mounted at /dev (%d nodes registered)\n", devfs_node_count);
    return 0;
}

static int devfs_unmount(void *fs_private)
{
    (void)fs_private;
    /* Nothing to free – node table is static. */
    return 0;
}

/* ---- open ---- */

static int devfs_open(void *fs_private, const char *path, int flags,
                      void **file_private)
{
    (void)fs_private;

    const char *name = strip_leading_slash(path);

    /* Opening the directory itself (path = "" or "/") */
    if (*name == '\0') {
        devfs_file_t *f = (devfs_file_t *)kalloc(sizeof(devfs_file_t));
        if (!f) return -1;
        f->node    = NULL;    /* marks this as the directory fd */
        f->offset  = 0;
        f->dir_pos = 0;
        *file_private = f;
        return 0;
    }

    devfs_node_t *node = devfs_find_node(name);
    if (!node) {
        printk("[devfs] open: no device named '%s'\n", name);
        return -1;
    }

    /* O_DIRECTORY on a non-directory is an error */
    if (flags & O_DIRECTORY) {
        printk("[devfs] open: '%s' is not a directory\n", name);
        return -1;
    }

    devfs_file_t *f = (devfs_file_t *)kalloc(sizeof(devfs_file_t));
    if (!f) return -1;

    f->node    = node;
    f->offset  = 0;
    f->dir_pos = 0;
    *file_private = f;
    return 0;
}

/* ---- close ---- */

static int devfs_close(void *file_private)
{
    if (file_private) kfree(file_private);
    return 0;
}

/* ---- read ---- */

static int devfs_read(void *file_private, void *buf, size_t count)
{
    devfs_file_t *f = (devfs_file_t *)file_private;
    if (!f || !f->node) return -1;

    devfs_node_t *node = f->node;

    if (node->type == DT_CHRDEV) {
        /* Read count characters one at a time */
        char *cbuf = (char *)buf;
        for (size_t i = 0; i < count; i++) {
            cbuf[i] = cread(node->dev_id, node->minor);
        }
        return (int)count;
    }

    if (node->type == DT_BLKDEV) {
        /* Block device: translate byte offset → sector number.
         * Sector size = 512 bytes. We read whole sectors only.
         * count must be a multiple of 512 for clean semantics; if not,
         * we round up and read the covering sectors. */
        if (count == 0) return 0;

        uint32_t sector     = f->offset / 512;
        uint32_t n_sectors  = (uint32_t)((count + 511) / 512);

        int ret = bread(node->dev_id, node->minor, buf, sector, n_sectors);
        if (ret > 0) f->offset += ret;
        return ret;
    }

    return -1;
}

/* ---- write ---- */

static int devfs_write(void *file_private, const void *buf, size_t count)
{
    devfs_file_t *f = (devfs_file_t *)file_private;
    if (!f || !f->node) return -1;

    devfs_node_t *node = f->node;

    if (node->type == DT_CHRDEV) {
        const char *cbuf = (const char *)buf;
        for (size_t i = 0; i < count; i++) {
            if (cwrite(node->dev_id, node->minor, cbuf[i]) != 0)
                return (int)i;   /* return chars written so far */
        }
        return (int)count;
    }

    if (node->type == DT_BLKDEV) {
        if (count == 0) return 0;

        uint32_t sector    = f->offset / 512;
        uint32_t n_sectors = (uint32_t)((count + 511) / 512);

        int ret = bwrite(node->dev_id, node->minor, buf, sector, n_sectors);
        if (ret > 0) f->offset += ret;
        return ret;
    }

    return -1;
}

/* ---- seek ---- */

static int devfs_seek(void *file_private, int32_t offset, int whence,
                      uint32_t *new_offset)
{
    devfs_file_t *f = (devfs_file_t *)file_private;
    if (!f || !f->node) return -1;

    /* seek is meaningful only for block devices */
    if (f->node->type != DT_BLKDEV) return -1;

    int32_t pos;
    switch (whence) {
    case SEEK_SET:
        if (offset < 0) return -1;
        pos = offset;
        break;
    case SEEK_CUR:
        pos = (int32_t)f->offset + offset;
        if (pos < 0) return -1;
        break;
    case SEEK_END:
        /* We don't know device size; treat as error */
        return -1;
    default:
        return -1;
    }

    f->offset   = (uint32_t)pos;
    *new_offset = f->offset;
    return 0;
}

/* ---- truncate ---- */

static int devfs_truncate(void *file_private, uint32_t length)
{
    (void)file_private;
    (void)length;
    /* Devices cannot be truncated */
    return -1;
}

/* ---- readdir ---- */

static int devfs_readdir(void *file_private, dirent_t *dirent)
{
    devfs_file_t *f = (devfs_file_t *)file_private;
    if (!f) return -1;

    /* Scan for the next in-use node starting from dir_pos */
    while (f->dir_pos < DEVFS_MAX_NODES) {
        int i = f->dir_pos++;
        if (!devfs_nodes[i].in_use) continue;

        strncpy(dirent->name, devfs_nodes[i].name, 255);
        dirent->name[255] = '\0';
        dirent->inode     = (uint32_t)i + 1;   /* 1-based synthetic inode */
        dirent->type      = devfs_nodes[i].type;
        return 1;   /* entry returned */
    }

    return 0;   /* end of directory */
}

/* ---- mkdir / rmdir / unlink / rename ---- */

static int devfs_mkdir(void *fs_private, const char *path, uint32_t mode)
{
    (void)fs_private; (void)path; (void)mode;
    return -1;   /* devfs is read-only: no subdirectories */
}

static int devfs_rmdir(void *fs_private, const char *path)
{
    (void)fs_private; (void)path;
    return -1;
}

static int devfs_unlink(void *fs_private, const char *path)
{
    (void)fs_private; (void)path;
    return -1;   /* use devfs_unregister_device() instead */
}

static int devfs_rename(void *fs_private, const char *old_path,
                        const char *new_path)
{
    (void)fs_private; (void)old_path; (void)new_path;
    return -1;
}

/* ---- stat ---- */

static int devfs_stat(void *fs_private, const char *path, stat_t *st)
{
    (void)fs_private;

    const char *name = strip_leading_slash(path);

    /* stat on the /dev directory itself */
    if (*name == '\0') {
        st->type  = DT_DIR;
        st->size  = 0;
        st->inode = 0;
        st->ctime = 0;
        st->mtime = 0;
        st->mode  = 0755;
        return 0;
    }

    devfs_node_t *node = devfs_find_node(name);
    if (!node) return -1;

    st->type  = node->type;
    st->size  = 0;
    st->inode = (uint32_t)(node - devfs_nodes) + 1;
    st->ctime = 0;
    st->mtime = 0;
    st->mode  = (node->type == DT_CHRDEV) ? 0600 : 0660;
    return 0;
}

/* =========================================================================
 * devfs Operations Table
 * ========================================================================= */

static fs_ops_t devfs_ops = {
    .mount    = devfs_mount,
    .unmount  = devfs_unmount,
    .open     = devfs_open,
    .close    = devfs_close,
    .read     = devfs_read,
    .write    = devfs_write,
    .seek     = devfs_seek,
    .truncate = devfs_truncate,
    .readdir  = devfs_readdir,
    .mkdir    = devfs_mkdir,
    .rmdir    = devfs_rmdir,
    .unlink   = devfs_unlink,
    .rename   = devfs_rename,
    .stat     = devfs_stat,
};

/* =========================================================================
 * Initialisation
 * ========================================================================= */

void devfs_init(void)
{
    /* Node table is already zeroed (static storage), so in_use=0 for all. */
    if (register_filesystem("devfs", &devfs_ops) < 0) {
        printk("[devfs] FAILED to register filesystem type\n");
        return;
    }

    if (fs_mount(-1, -1, "devfs", "/dev") != 0) {
        printk("[devfs] FAILED to mount at /dev\n");
        return;
    }
}