/* ============================================================================
 * tmpfs - in-memory filesystem
 *
 * A simple RAM-backed filesystem with files and nested directories.  Every
 * inode is a heap-allocated node; regular files store their bytes in a
 * growable kernel buffer.  Used for /tmp and any scratch storage.
 * ============================================================================ */

#include "fs/tmpfs.h"
#include "fs/fs.h"
#include "kernel/sched.h"
#include "mm/slab.h"
#include "lib/list.h"
#include "lib/string.h"
#include "lib/printk.h"
#include <stdint.h>
#include <stddef.h>

/* ============================================================================
 * Inode structure
 * ============================================================================ */

typedef struct tmpfs_node {
    char       name[64];
    uint8_t    type;        /* DT_REG / DT_DIR */
    uint32_t   inode;       /* synthetic inode number */
    uint32_t   size;        /* file size in bytes */
    uint32_t   mtime;       /* seconds since boot (approx) */
    uint8_t   *data;        /* regular file contents */
    uint32_t   capacity;    /* allocated bytes for data */

    int        refcount;    /* 1 (directory link) + open handles */

    struct tmpfs_node *parent; /* directory containing us (NULL for root) */
    list_head_t   children;    /* head of child list (directories only) */
    list_head_t   list;        /* node in parent's children list */
} tmpfs_node_t;

/* ============================================================================
 * Per-open-file state
 * ============================================================================ */

typedef struct {
    tmpfs_node_t *node;
    uint32_t      offset;
} tmpfs_file_t;

static uint32_t tmpfs_next_inode = 1;

/* ============================================================================
 * Node lifecycle
 * ============================================================================ */

static tmpfs_node_t *node_alloc(const char *name, uint8_t type)
{
    if (strlen(name) >= sizeof(((tmpfs_node_t *)0)->name))
        return NULL;

    tmpfs_node_t *n = (tmpfs_node_t *)kalloc(sizeof(tmpfs_node_t));
    if (!n) return NULL;

    memset(n, 0, sizeof(*n));
    strncpy(n->name, name, sizeof(n->name) - 1);
    n->name[sizeof(n->name) - 1] = '\0';
    n->type      = type;
    n->inode     = tmpfs_next_inode++;
    n->refcount  = 1;
    INIT_LIST_HEAD(&n->children);
    INIT_LIST_HEAD(&n->list);
    return n;
}

/* Free a node and its data once refcount drops to zero. */
static void node_put(tmpfs_node_t *n)
{
    if (!n) return;
    if (--n->refcount > 0) return;
    if (n->data) kfree(n->data);
    kfree(n);
}

/* Recursively free a directory tree (unmount). */
static void tree_free(tmpfs_node_t *n)
{
    if (!n) return;
    if (n->type == DT_DIR) {
        list_head_t *pos, *tmp;
        list_for_each_safe(pos, tmp, &n->children) {
            tmpfs_node_t *c = list_entry(pos, tmpfs_node_t, list);
            list_del(&c->list);
            tree_free(c);
        }
    }
    if (n->data) kfree(n->data);
    kfree(n);
}

/* ============================================================================
 * File data management
 * ============================================================================ */

/* Grow the file buffer to hold at least `need` bytes. */
static int node_reserve(tmpfs_node_t *n, uint32_t need)
{
    if (need <= n->capacity)
        return 0;

    uint32_t ncap = n->capacity ? n->capacity : 64;
    while (ncap < need)
        ncap *= 2;

    uint8_t *nd = (uint8_t *)kalloc(ncap);
    if (!nd) return -1;

    if (n->data && n->size) {
        memcpy(nd, n->data, n->size);
        kfree(n->data);
    }
    n->data     = nd;
    n->capacity = ncap;
    return 0;
}

/* ============================================================================
 * Path resolution
 * ============================================================================ */

static int is_dir(tmpfs_node_t *n)
{
    return n->type == DT_DIR;
}

static tmpfs_node_t *lookup_child(tmpfs_node_t *dir, const char *name)
{
    if (!is_dir(dir)) return NULL;
    list_head_t *pos;
    list_for_each(pos, &dir->children) {
        tmpfs_node_t *c = list_entry(pos, tmpfs_node_t, list);
        if (strcmp(c->name, name) == 0)
            return c;
    }
    return NULL;
}

/* Resolve a full relative path from the mount root to a node. */
static tmpfs_node_t *lookup(tmpfs_node_t *root, const char *path)
{
    if (!root) return NULL;
    const char *p = path;
    while (*p == '/') p++;

    tmpfs_node_t *cur = root;
    while (*p) {
        const char *start = p;
        while (*p && *p != '/') p++;
        size_t len = (size_t)(p - start);
        if (len == 0) { p++; continue; }
        if (len >= 64) return NULL;

        char comp[64];
        memcpy(comp, start, len);
        comp[len] = '\0';

        tmpfs_node_t *child = lookup_child(cur, comp);
        if (!child) return NULL;
        cur = child;
        while (*p == '/') p++;
    }
    return cur;
}

/* Resolve the parent dir + leaf name for a path. */
static tmpfs_node_t *lookup_parent(tmpfs_node_t *root, const char *path,
                                   char *leaf)
{
    if (!root) return NULL;
    const char *p = path;
    while (*p == '/') p++;

    tmpfs_node_t *cur = root;
    while (*p) {
        const char *start = p;
        while (*p && *p != '/') p++;
        size_t len = (size_t)(p - start);
        if (len == 0) { p++; continue; }
        if (len >= 64) return NULL;

        /* Look ahead: is this the last component? */
        const char *after = p;
        while (*after == '/') after++;
        if (!*after) {
            memcpy(leaf, start, len);
            leaf[len] = '\0';
            return cur;
        }

        char comp[64];
        memcpy(comp, start, len);
        comp[len] = '\0';
        tmpfs_node_t *child = lookup_child(cur, comp);
        if (!child || !is_dir(child)) return NULL;
        cur = child;
        p = after;
    }

    /* Path was "/" or empty: no leaf. */
    return NULL;
}

/* ============================================================================
 * fs_ops callbacks
 * ============================================================================ */

static uint32_t now_secs(void)
{
    extern uint32_t arch_timer_get_ticks(void);
    return arch_timer_get_ticks() / 100;
}

static int tmpfs_mount(int dev_id, int part_id, void **fs_private)
{
    (void)dev_id; (void)part_id;

    tmpfs_node_t *root = node_alloc("", DT_DIR);
    if (!root) return -1;

    *fs_private = root;
    printk("[tmpfs] mounted (in-memory filesystem)\n");
    return 0;
}

static int tmpfs_unmount(void *fs_private)
{
    tree_free((tmpfs_node_t *)fs_private);
    return 0;
}

static int tmpfs_open(void *fs_private, const char *path, int flags,
                      uint32_t mode, void **file_private)
{
    (void)mode;
    tmpfs_node_t *root = (tmpfs_node_t *)fs_private;

    /* Opening the root directory. */
    const char *p = path;
    while (*p == '/') p++;
    if (*p == '\0') {
        if (flags & O_DIRECTORY) {
            tmpfs_file_t *f = (tmpfs_file_t *)kalloc(sizeof(tmpfs_file_t));
            if (!f) return -1;
            f->node = root;
            f->offset = 0;
            root->refcount++;
            *file_private = f;
            return 0;
        }
        return -1;   /* cannot open "/" as a file */
    }

    tmpfs_node_t *n = lookup(root, path);
    if (!n && (flags & O_CREAT)) {
        /* Create the leaf in its parent. */
        char leaf[64];
        tmpfs_node_t *parent = lookup_parent(root, path, leaf);
        if (!parent) return -1;
        n = node_alloc(leaf, DT_REG);
        if (!n) return -1;
        n->parent = parent;
        n->mtime  = now_secs();
        list_add_tail(&n->list, &parent->children);
    }
    if (!n) return -1;

    if ((flags & O_DIRECTORY) && !is_dir(n))
        return -1;
    if (!(flags & O_DIRECTORY) && is_dir(n))
        return -1;

    /* O_TRUNC: empty the file. */
    if ((flags & O_TRUNC) && n->type == DT_REG)
        n->size = 0;

    tmpfs_file_t *f = (tmpfs_file_t *)kalloc(sizeof(tmpfs_file_t));
    if (!f) return -1;
    f->node   = n;
    f->offset = 0;
    n->refcount++;
    *file_private = f;
    return 0;
}

static int tmpfs_close(void *file_private)
{
    tmpfs_file_t *f = (tmpfs_file_t *)file_private;
    if (!f) return 0;
    node_put(f->node);
    kfree(f);
    return 0;
}

static int tmpfs_read(void *file_private, void *buf, size_t count)
{
    tmpfs_file_t *f = (tmpfs_file_t *)file_private;
    if (!f || !f->node) return -1;
    tmpfs_node_t *n = f->node;
    if (n->type != DT_REG) return -1;

    if (f->offset >= n->size)
        return 0;

    size_t r = count;
    if (f->offset + r > n->size)
        r = n->size - f->offset;
    memcpy(buf, n->data + f->offset, r);
    f->offset += (uint32_t)r;
    return (int)r;
}

static int tmpfs_write(void *file_private, const void *buf, size_t count)
{
    tmpfs_file_t *f = (tmpfs_file_t *)file_private;
    if (!f || !f->node) return -1;
    tmpfs_node_t *n = f->node;
    if (n->type != DT_REG) return -1;
    if (count == 0) return 0;

    if (node_reserve(n, f->offset + (uint32_t)count) < 0)
        return -1;

    memcpy(n->data + f->offset, buf, count);
    f->offset += (uint32_t)count;
    if (f->offset > n->size)
        n->size = f->offset;
    n->mtime = now_secs();
    return (int)count;
}

static int tmpfs_seek(void *file_private, int32_t offset, int whence,
                      uint32_t *new_offset)
{
    tmpfs_file_t *f = (tmpfs_file_t *)file_private;
    if (!f || !f->node) return -1;
    tmpfs_node_t *n = f->node;

    int32_t pos;
    switch (whence) {
        case SEEK_SET:
            pos = offset;
            break;
        case SEEK_CUR:
            pos = (int32_t)f->offset + offset;
            break;
        case SEEK_END:
            pos = (int32_t)n->size + offset;
            break;
        default:
            return -1;
    }
    if (pos < 0) return -1;
    f->offset = (uint32_t)pos;
    *new_offset = f->offset;
    return 0;
}

static int tmpfs_truncate(void *file_private, uint32_t length)
{
    tmpfs_file_t *f = (tmpfs_file_t *)file_private;
    if (!f || !f->node) return -1;
    tmpfs_node_t *n = f->node;
    if (n->type != DT_REG) return -1;

    if (length > n->size) {
        if (node_reserve(n, length) < 0) return -1;
        memset(n->data + n->size, 0, length - n->size);
    }
    n->size = length;
    n->mtime = now_secs();
    return 0;
}

static int tmpfs_readdir(void *file_private, dirent_t *dirent)
{
    tmpfs_file_t *f = (tmpfs_file_t *)file_private;
    if (!f || !f->node || f->node->type != DT_DIR) return -1;

    tmpfs_node_t *dir = f->node;
    /* f->offset is reused as the iteration cursor for readdir. */
    uint32_t idx = f->offset;
    uint32_t i = 0;
    list_head_t *pos;
    list_for_each(pos, &dir->children) {
        tmpfs_node_t *c = list_entry(pos, tmpfs_node_t, list);
        if (i++ < idx) continue;
        strncpy(dirent->name, c->name, 255);
        dirent->name[255] = '\0';
        dirent->inode = c->inode;
        dirent->type  = c->type;
        f->offset = idx + 1;
        return 1;
    }
    return 0;   /* end */
}

static int tmpfs_mkdir(void *fs_private, const char *path, uint32_t mode)
{
    (void)mode;
    tmpfs_node_t *root = (tmpfs_node_t *)fs_private;

    if (lookup(root, path))
        return -1;   /* exists */

    char leaf[64];
    tmpfs_node_t *parent = lookup_parent(root, path, leaf);
    if (!parent) return -1;

    tmpfs_node_t *n = node_alloc(leaf, DT_DIR);
    if (!n) return -1;
    n->parent = parent;
    n->mtime  = now_secs();
    list_add_tail(&n->list, &parent->children);
    return 0;
}

static int tmpfs_rmdir(void *fs_private, const char *path)
{
    tmpfs_node_t *root = (tmpfs_node_t *)fs_private;
    tmpfs_node_t *n = lookup(root, path);
    if (!n || n->type != DT_DIR) return -1;
    if (!list_empty(&n->children)) return -1;   /* not empty */

    list_del(&n->list);
    n->parent = NULL;
    node_put(n);   /* drop the directory-link ref */
    return 0;
}

static int tmpfs_unlink(void *fs_private, const char *path)
{
    tmpfs_node_t *root = (tmpfs_node_t *)fs_private;
    tmpfs_node_t *n = lookup(root, path);
    if (!n || n->type != DT_REG) return -1;

    list_del(&n->list);
    n->parent = NULL;
    node_put(n);
    return 0;
}

static int tmpfs_rename(void *fs_private, const char *old_path,
                        const char *new_path)
{
    tmpfs_node_t *root = (tmpfs_node_t *)fs_private;
    tmpfs_node_t *n = lookup(root, old_path);
    if (!n) return -1;

    char newleaf[64];
    tmpfs_node_t *newparent = lookup_parent(root, new_path, newleaf);
    if (!newparent) return -1;

    /* Refuse to move a directory into itself. */
    if (is_dir(n)) {
        tmpfs_node_t *walk = newparent;
        while (walk) {
            if (walk == n) return -1;
            walk = walk->parent;
        }
    }

    list_del(&n->list);
    n->parent = newparent;
    strncpy(n->name, newleaf, sizeof(n->name) - 1);
    n->name[sizeof(n->name) - 1] = '\0';
    list_add_tail(&n->list, &newparent->children);
    return 0;
}

static void node_stat(tmpfs_node_t *n, stat_t *st)
{
    st->type  = n->type;
    st->size  = (n->type == DT_REG) ? n->size : 0;
    st->inode = n->inode;
    st->ctime = n->mtime;
    st->mtime = n->mtime;
    st->mode  = (n->type == DT_DIR) ? 0777 : 0666;
    st->uid   = 0;
    st->gid   = 0;
}

static int tmpfs_stat(void *fs_private, const char *path, stat_t *st)
{
    tmpfs_node_t *root = (tmpfs_node_t *)fs_private;

    const char *p = path;
    while (*p == '/') p++;
    if (*p == '\0') {
        node_stat(root, st);
        return 0;
    }

    tmpfs_node_t *n = lookup(root, path);
    if (!n) return -1;
    node_stat(n, st);
    return 0;
}

static int tmpfs_fstat(void *file_private, stat_t *st)
{
    tmpfs_file_t *f = (tmpfs_file_t *)file_private;
    if (!f || !f->node) return -1;
    node_stat(f->node, st);
    return 0;
}

static int tmpfs_chmod(void *fs_private, const char *path, uint32_t mode)
{
    (void)fs_private; (void)path; (void)mode;
    return 0;   /* no permission enforcement in tmpfs */
}

static int tmpfs_fchmod(void *file_private, uint32_t mode)
{
    (void)file_private; (void)mode;
    return 0;
}

static int tmpfs_mknod(void *fs_private, const char *path, uint32_t mode,
                       uint32_t dev)
{
    (void)fs_private; (void)path; (void)mode; (void)dev;
    return -1;   /* device nodes live in devfs, not tmpfs */
}

static int tmpfs_utime(void *fs_private, const char *path,
                       uint32_t atime, uint32_t mtime)
{
    (void)atime;
    tmpfs_node_t *root = (tmpfs_node_t *)fs_private;
    tmpfs_node_t *n = lookup(root, path);
    if (!n) return -1;
    n->mtime = mtime;
    return 0;
}

static int tmpfs_ioctl(void *file_private, uint32_t cmd, uint32_t arg)
{
    (void)file_private; (void)cmd; (void)arg;
    return -1;
}

static int tmpfs_poll(void *file_private)
{
    (void)file_private;
    return 1;   /* tmpfs files are always ready */
}

static fs_ops_t tmpfs_ops = {
    .mount    = tmpfs_mount,
    .unmount  = tmpfs_unmount,
    .open     = tmpfs_open,
    .close    = tmpfs_close,
    .read     = tmpfs_read,
    .write    = tmpfs_write,
    .seek     = tmpfs_seek,
    .truncate = tmpfs_truncate,
    .ioctl    = tmpfs_ioctl,
    .readdir  = tmpfs_readdir,
    .mkdir    = tmpfs_mkdir,
    .rmdir    = tmpfs_rmdir,
    .unlink   = tmpfs_unlink,
    .rename   = tmpfs_rename,
    .stat     = tmpfs_stat,
    .fstat    = tmpfs_fstat,
    .chmod    = tmpfs_chmod,
    .fchmod   = tmpfs_fchmod,
    .mknod    = tmpfs_mknod,
    .utime    = tmpfs_utime,
    .poll     = tmpfs_poll,
};

void tmpfs_init(void)
{
    if (register_pseudo_filesystem("tmpfs", &tmpfs_ops) < 0) {
        printk("[tmpfs] FAILED to register filesystem type\n");
        return;
    }
    printk("[tmpfs] filesystem type registered\n");
}
