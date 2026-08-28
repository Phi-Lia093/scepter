/* ============================================================================
 * ext2 Filesystem Driver - VFS Integration
 * ============================================================================ */

#include "fs/ext2.h"
#include "fs/fs.h"
#include "driver/block/block.h"
#include "driver/char/rtc.h"
#include "mm/slab.h"
#include "lib/printk.h"
#include "lib/string.h"

/* Forward declarations (helpers are defined later in this file) */
static int parse_parent_and_name(const char *path, char *parent, char *name);
static int lookup_directory_by_path(ext2_fs_info_t *fs, const char *path,
                                    uint32_t *ino_out,
                                    struct ext2_inode *inode_out);

/* Map an ext2 mode to an EXT2_FT_* directory entry type. */
static uint8_t ext2_mode_to_ftype(uint16_t mode)
{
    if (EXT2_ISREG(mode))  return EXT2_FT_REG_FILE;
    if (EXT2_ISDIR(mode))  return EXT2_FT_DIR;
    if (EXT2_ISCHR(mode))  return EXT2_FT_CHRDEV;
    if (EXT2_ISBLK(mode))  return EXT2_FT_BLKDEV;
    if (EXT2_ISFIFO(mode)) return EXT2_FT_FIFO;
    if (EXT2_ISSOCK(mode)) return EXT2_FT_SOCK;
    if (EXT2_ISLNK(mode))  return EXT2_FT_SYMLINK;
    return EXT2_FT_UNKNOWN;
}

/* ============================================================================
 * VFS Callback: open
 * ============================================================================ */

static int ext2_vfs_open(void *fs_private, const char *path, int flags,
                         uint32_t mode, void **file_private)
{
    ext2_fs_info_t *fs = (ext2_fs_info_t *)fs_private;
    if (!fs || !path || !file_private) return -1;

    ext2_file_info_t *file =
        (ext2_file_info_t *)kalloc(sizeof(ext2_file_info_t));
    if (!file) {
        printk("[ext2] Failed to allocate file info\n");
        return -1;
    }
    file->fs = fs;
    file->offset = 0;
    file->dir_pos = 0;
    file->dirty = 0;

    const char *filename = path;
    if (filename[0] == '/') filename++;

    /* Root directory. */
    if (filename[0] == '\0' || strcmp(filename, ".") == 0) {
        file->inode_num = EXT2_ROOT_INO;
        if (ext2_read_inode(fs, EXT2_ROOT_INO, &file->inode) < 0) {
            kfree(file);
            return -1;
        }
        *file_private = file;
        return 0;
    }

    /* Navigate path components. */
    struct ext2_inode dir_inode;
    uint32_t current_ino = EXT2_ROOT_INO;

    if (ext2_read_inode(fs, current_ino, &dir_inode) < 0) {
        kfree(file);
        return -1;
    }

    char path_copy[256];
    strncpy(path_copy, filename, sizeof(path_copy) - 1);
    path_copy[sizeof(path_copy) - 1] = '\0';

    char *token = path_copy;
    char *next = token;

    while (*token) {
        while (*next && *next != '/') next++;
        int is_last = (*next == '\0');
        *next = '\0';

        if (token[0] == '\0') {
            if (!is_last) {
                token = next + 1;
                next = token;
            }
            continue;
        }

        uint32_t next_ino;
        if (ext2_lookup(fs, &dir_inode, token, &next_ino) < 0) {
            /* Component not found. */
            if (is_last && (flags & O_CREAT)) {
                uint16_t perm = (mode & 0777) ? (mode & 0777) : 0644;
                uint16_t mode_bits = EXT2_S_IFREG | perm;
                uint32_t parent_ino = current_ino;
                uint32_t pref = (parent_ino - 1) / fs->inodes_per_group;

                if (ext2_alloc_inode(fs, mode_bits, pref, &next_ino) < 0) {
                    printk("[ext2] Failed to allocate inode\n");
                    kfree(file);
                    return -1;
                }
                if (ext2_add_dirent(fs, parent_ino, &dir_inode, token,
                                    next_ino, EXT2_FT_REG_FILE) < 0) {
                    printk("[ext2] Failed to add directory entry\n");
                    ext2_free_inode(fs, next_ino);
                    kfree(file);
                    return -1;
                }
                if (ext2_write_inode(fs, parent_ino, &dir_inode) < 0) {
                    kfree(file);
                    return -1;
                }
                ext2_sync_bitmaps(fs);
            } else {
                kfree(file);
                return -1;
            }
        }

        current_ino = next_ino;

        if (ext2_read_inode(fs, current_ino, &dir_inode) < 0) {
            kfree(file);
            return -1;
        }

        if (!is_last && !EXT2_ISDIR(dir_inode.i_mode)) {
            kfree(file);
            return -1;   /* Not a directory */
        }
        if (is_last) break;

        token = next + 1;
        next = token;
    }

    /* Found the target. */
    file->inode_num = current_ino;
    file->inode = dir_inode;

    /* O_TRUNC - truncate to 0 when opened for writing. */
    if ((flags & O_TRUNC) && EXT2_ISREG(file->inode.i_mode)) {
        if (ext2_truncate_file(fs, file->inode_num, &file->inode, 0) < 0) {
            kfree(file);
            return -1;
        }
        file->dirty = 1;
    }

    /* O_APPEND - every write starts at EOF. */
    if (flags & O_APPEND)
        file->offset = file->inode.i_size;

    *file_private = file;
    return 0;
}

/* ============================================================================
 * VFS Callback: close
 * ============================================================================ */

static int ext2_vfs_close(void *file_private)
{
    if (!file_private) return -1;

    ext2_file_info_t *file = (ext2_file_info_t *)file_private;

    if (file->dirty) {
        if (ext2_write_inode(file->fs, file->inode_num, &file->inode) < 0) {
            printk("[ext2] Failed to write back inode on close\n");
        }
        ext2_sync_bitmaps(file->fs);
    }

    kfree(file);
    return 0;
}

/* ============================================================================
 * VFS Callbacks: read / write / seek / truncate
 * ============================================================================ */

static int ext2_vfs_read(void *file_private, void *buf, size_t count)
{
    if (!file_private || !buf) return -1;
    ext2_file_info_t *file = (ext2_file_info_t *)file_private;

    if (!EXT2_ISREG(file->inode.i_mode) && !EXT2_ISLNK(file->inode.i_mode))
        return -1;

    int bytes_read = ext2_read_file(file->fs, &file->inode, file->offset,
                                    (uint8_t *)buf, count);
    if (bytes_read > 0)
        file->offset += bytes_read;
    return bytes_read;
}

static int ext2_vfs_write(void *file_private, const void *buf, size_t count)
{
    if (!file_private || !buf) return -1;
    ext2_file_info_t *file = (ext2_file_info_t *)file_private;

    if (!EXT2_ISREG(file->inode.i_mode))
        return -1;

    int bytes_written = ext2_write_file(file->fs, file->inode_num,
                                        &file->inode, file->offset,
                                        (const uint8_t *)buf, count);
    if (bytes_written > 0) {
        file->offset += bytes_written;
        file->dirty = 1;
    }
    return bytes_written;
}

static int ext2_vfs_seek(void *file_private, int32_t offset, int whence,
                         uint32_t *new_offset)
{
    if (!file_private || !new_offset) return -1;
    ext2_file_info_t *file = (ext2_file_info_t *)file_private;

    int32_t target;
    switch (whence) {
    case SEEK_SET: target = offset; break;
    case SEEK_CUR: target = file->offset + offset; break;
    case SEEK_END: target = file->inode.i_size + offset; break;
    default: return -1;
    }
    if (target < 0) return -1;

    if (EXT2_ISDIR(file->inode.i_mode)) {
        /* Directory fds use byte offsets (rewinddir / seekdir). */
        file->dir_pos = (uint32_t)target;
        *new_offset = file->dir_pos;
        return 0;
    }

    file->offset = (uint32_t)target;
    *new_offset = file->offset;
    return 0;
}

static int ext2_vfs_truncate(void *file_private, uint32_t length)
{
    if (!file_private) return -1;
    ext2_file_info_t *file = (ext2_file_info_t *)file_private;

    if (ext2_truncate_file(file->fs, file->inode_num, &file->inode, length) < 0)
        return -1;

    file->dirty = 1;
    if (file->offset > length)
        file->offset = length;
    return 0;
}

/* ============================================================================
 * VFS Callback: readdir
 *
 * dir_pos is a byte offset into the directory data; entries are packed with
 * varying rec_len, so we walk the chain one entry at a time.
 * ============================================================================ */

static int ext2_vfs_readdir(void *file_private, dirent_t *dirent)
{
    if (!file_private || !dirent) return -1;
    ext2_file_info_t *file = (ext2_file_info_t *)file_private;

    if (!EXT2_ISDIR(file->inode.i_mode))
        return -1;

    ext2_fs_info_t *fs = file->fs;
    uint32_t dir_size = file->inode.i_size;
    uint8_t buf[4096];
    uint32_t cur_block = ~0u;   /* file block currently in buf */

    for (;;) {
        if (file->dir_pos >= dir_size)
            return 0;   /* end of directory */

        uint32_t b = file->dir_pos / fs->block_size;
        uint32_t off = file->dir_pos % fs->block_size;

        if (b != cur_block) {
            uint32_t dblk;
            if (ext2_bmap(fs, &file->inode, b, &dblk) < 0)
                return -1;
            if (dblk == 0) {
                /* Sparse block: skip the whole block. */
                file->dir_pos = (b + 1) * fs->block_size;
                continue;
            }
            if (ext2_read_block(fs, dblk, buf) < 0)
                return -1;
            cur_block = b;
        }

        /* Parse the entry at `off`. */
        uint32_t ino, rl;
        uint8_t nl, ft;
        const char *nm;
        if (fs->sb.s_feature_incompat & EXT2_FEATURE_INCOMPAT_FILETYPE) {
            const struct ext2_dirent *d =
                (const struct ext2_dirent *)(buf + off);
            ino = d->inode;
            rl = d->rec_len;
            nl = d->name_len;
            ft = d->file_type;
            nm = d->name;
        } else {
            const struct ext2_dirent_legacy *l =
                (const struct ext2_dirent_legacy *)(buf + off);
            ino = l->inode;
            rl = l->rec_len;
            nl = l->name_len;
            ft = EXT2_FT_UNKNOWN;
            nm = l->name;
        }

        if (rl < 8 || off + rl > fs->block_size) {
            printk("[ext2] Corrupt dirent (off %u, rec_len %u)\n", off, rl);
            return -1;
        }

        /* Advance before reporting, so errors never loop forever. */
        file->dir_pos += rl;

        if (ino == 0)
            continue;   /* deleted entry */

        memcpy(dirent->name, nm, nl);
        dirent->name[nl] = '\0';
        dirent->inode = ino;

        switch (ft) {
        case EXT2_FT_REG_FILE: dirent->type = DT_REG; break;
        case EXT2_FT_DIR:      dirent->type = DT_DIR; break;
        case EXT2_FT_CHRDEV:   dirent->type = DT_CHRDEV; break;
        case EXT2_FT_BLKDEV:   dirent->type = DT_BLKDEV; break;
        case EXT2_FT_SYMLINK:  dirent->type = DT_SYMLINK; break;
        case EXT2_FT_FIFO:     dirent->type = DT_FIFO; break;
        default:
            /* No file_type info (legacy dirs): read the inode. */
            {
                struct ext2_inode ei;
                stat_t st;
                if (ext2_read_inode(fs, ino, &ei) == 0) {
                    ext2_fill_stat(&ei, ino, &st);
                    dirent->type = st.type;
                } else {
                    dirent->type = DT_UNKNOWN;
                }
            }
            break;
        }

        return 1;
    }
}

/* ============================================================================
 * VFS Callback: mkdir
 * ============================================================================ */

/* Check a directory contains nothing but '.' and '..'. */
static int ext2_dir_is_empty(ext2_fs_info_t *fs, struct ext2_inode *dir)
{
    uint32_t num_blocks = (dir->i_size + fs->block_size - 1) / fs->block_size;
    uint8_t buf[4096];

    for (uint32_t b = 0; b < num_blocks; b++) {
        uint32_t dblk;
        if (ext2_bmap(fs, dir, b, &dblk) < 0)
            return 0;
        if (dblk == 0)
            continue;
        if (ext2_read_block(fs, dblk, buf) < 0)
            return 0;

        uint32_t off = 0;
        while (off + 8 <= fs->block_size) {
            uint32_t ino, rl;
            uint8_t nl;
            const char *nm;
            if (fs->sb.s_feature_incompat & EXT2_FEATURE_INCOMPAT_FILETYPE) {
                const struct ext2_dirent *d =
                    (const struct ext2_dirent *)(buf + off);
                ino = d->inode; rl = d->rec_len; nl = d->name_len; nm = d->name;
            } else {
                const struct ext2_dirent_legacy *l =
                    (const struct ext2_dirent_legacy *)(buf + off);
                ino = l->inode; rl = l->rec_len; nl = l->name_len; nm = l->name;
            }
            if (rl < 8 || off + rl > fs->block_size)
                return 0;
            if (ino != 0) {
                if (!((nl == 1 && memcmp(nm, ".", 1) == 0) ||
                      (nl == 2 && memcmp(nm, "..", 2) == 0)))
                    return 0;   /* not empty */
            }
            off += rl;
        }
    }
    return 1;
}

static int ext2_vfs_mkdir(void *fs_private, const char *path, uint32_t mode)
{
    ext2_fs_info_t *fs = (ext2_fs_info_t *)fs_private;
    if (!fs || !path) return -1;

    char parent_path[256];
    char name[EXT2_NAME_LEN + 1];
    parse_parent_and_name(path, parent_path, name);
    if (name[0] == '\0' || strcmp(name, ".") == 0 ||
        strcmp(name, "..") == 0 || strchr(name, '/') != NULL)
        return -1;

    uint32_t parent_ino;
    struct ext2_inode parent_inode;
    if (lookup_directory_by_path(fs, parent_path, &parent_ino,
                                 &parent_inode) < 0)
        return -1;
    if (!EXT2_ISDIR(parent_inode.i_mode))
        return -1;

    uint32_t existing;
    if (ext2_lookup(fs, &parent_inode, name, &existing) == 0)
        return -1;   /* already exists */

    /* Allocate a new inode, preferring the parent's group. */
    uint32_t new_ino;
    uint32_t pref_group = (parent_ino - 1) / fs->inodes_per_group;
    uint16_t dir_mode = EXT2_S_IFDIR | (mode & 0777);
    if (ext2_alloc_inode(fs, dir_mode, pref_group, &new_ino) < 0)
        return -1;

    struct ext2_inode dir_inode;
    if (ext2_read_inode(fs, new_ino, &dir_inode) < 0) {
        ext2_free_inode(fs, new_ino);
        return -1;
    }

    /* Allocate the first data block and initialise '.'/'..'. */
    uint32_t dblk;
    if (ext2_alloc_block(fs, new_ino, &dblk) < 0) {
        ext2_free_inode(fs, new_ino);
        return -1;
    }
    dir_inode.i_block[0] = dblk;
    dir_inode.i_size = fs->block_size;
    dir_inode.i_links_count = 2;
    dir_inode.i_blocks = fs->block_size / 512;
    if (ext2_init_dir(fs, &dir_inode, new_ino, parent_ino) < 0) {
        ext2_free_block(fs, dblk);
        ext2_free_inode(fs, new_ino);
        return -1;
    }
    if (ext2_write_inode(fs, new_ino, &dir_inode) < 0) {
        ext2_free_block(fs, dblk);
        ext2_free_inode(fs, new_ino);
        return -1;
    }

    /* Link it into the parent. */
    if (ext2_add_dirent(fs, parent_ino, &parent_inode, name, new_ino,
                        EXT2_FT_DIR) < 0) {
        ext2_remove_dirent(fs, &dir_inode, ".");   /* not added to parent */
        ext2_free_block(fs, dblk);
        ext2_free_inode(fs, new_ino);
        return -1;
    }
    parent_inode.i_links_count++;
    parent_inode.i_mtime = rtc_get_unix_time();
    if (ext2_write_inode(fs, parent_ino, &parent_inode) < 0)
        return -1;

    ext2_sync_bitmaps(fs);
    return 0;
}

/* ============================================================================
 * VFS Callback: rmdir
 * ============================================================================ */

static int ext2_vfs_rmdir(void *fs_private, const char *path)
{
    ext2_fs_info_t *fs = (ext2_fs_info_t *)fs_private;
    if (!fs || !path) return -1;

    char parent_path[256];
    char name[EXT2_NAME_LEN + 1];
    parse_parent_and_name(path, parent_path, name);
    if (name[0] == '\0' || strcmp(name, ".") == 0 ||
        strcmp(name, "..") == 0 || strchr(name, '/') != NULL)
        return -1;

    uint32_t parent_ino;
    struct ext2_inode parent_inode;
    if (lookup_directory_by_path(fs, parent_path, &parent_ino,
                                 &parent_inode) < 0)
        return -1;

    uint32_t dir_ino;
    if (ext2_lookup(fs, &parent_inode, name, &dir_ino) < 0)
        return -1;

    struct ext2_inode dir_inode;
    if (ext2_read_inode(fs, dir_ino, &dir_inode) < 0)
        return -1;
    if (!EXT2_ISDIR(dir_inode.i_mode))
        return -1;
    if (!ext2_dir_is_empty(fs, &dir_inode))
        return -1;

    /* Remove from parent. */
    if (ext2_remove_dirent(fs, &parent_inode, name) < 0)
        return -1;
    parent_inode.i_links_count--;
    parent_inode.i_mtime = rtc_get_unix_time();
    if (ext2_write_inode(fs, parent_ino, &parent_inode) < 0)
        return -1;

    /* Free the directory's blocks and inode. */
    if (ext2_truncate_file(fs, dir_ino, &dir_inode, 0) < 0)
        return -1;
    if (ext2_write_inode(fs, dir_ino, &dir_inode) < 0)
        return -1;
    if (ext2_free_inode(fs, dir_ino) < 0)
        return -1;

    ext2_sync_bitmaps(fs);
    return 0;
}

/* ============================================================================
 * VFS Callback: unlink
 * ============================================================================ */

static int ext2_vfs_unlink(void *fs_private, const char *path)
{
    ext2_fs_info_t *fs = (ext2_fs_info_t *)fs_private;
    if (!fs || !path) return -1;

    char parent_path[256];
    char name[EXT2_NAME_LEN + 1];
    parse_parent_and_name(path, parent_path, name);
    if (name[0] == '\0' || strcmp(name, ".") == 0 ||
        strcmp(name, "..") == 0 || strchr(name, '/') != NULL)
        return -1;

    uint32_t parent_ino;
    struct ext2_inode parent_inode;
    if (lookup_directory_by_path(fs, parent_path, &parent_ino,
                                 &parent_inode) < 0)
        return -1;

    uint32_t ino;
    if (ext2_lookup(fs, &parent_inode, name, &ino) < 0)
        return -1;

    struct ext2_inode inode;
    if (ext2_read_inode(fs, ino, &inode) < 0)
        return -1;
    if (EXT2_ISDIR(inode.i_mode))
        return -1;   /* directories go through rmdir */

    /* Remove the directory entry. */
    if (ext2_remove_dirent(fs, &parent_inode, name) < 0)
        return -1;
    parent_inode.i_mtime = rtc_get_unix_time();
    if (ext2_write_inode(fs, parent_ino, &parent_inode) < 0)
        return -1;

    /* Decrement link count; free when it reaches zero. */
    inode.i_links_count--;
    if (inode.i_links_count == 0) {
        if (ext2_truncate_file(fs, ino, &inode, 0) < 0)
            return -1;
        if (ext2_write_inode(fs, ino, &inode) < 0)
            return -1;
        if (ext2_free_inode(fs, ino) < 0)
            return -1;
    } else {
        inode.i_ctime = rtc_get_unix_time();
        if (ext2_write_inode(fs, ino, &inode) < 0)
            return -1;
    }

    ext2_sync_bitmaps(fs);
    return 0;
}

/* ============================================================================
 * VFS Callback: rename (move/rename file or directory)
 * ============================================================================ */

/* Update the ".." entry of a directory to point at new_parent_ino. */
static int ext2_update_dotdot(ext2_fs_info_t *fs, uint32_t dir_ino,
                              struct ext2_inode *dir, uint32_t new_parent_ino)
{
    (void)dir_ino;
    uint32_t dblk;
    uint8_t buf[4096];
    if (ext2_bmap(fs, dir, 0, &dblk) < 0 || dblk == 0)
        return -1;
    if (ext2_read_block(fs, dblk, buf) < 0)
        return -1;

    /* The '.' entry comes first; '..' follows it. */
    uint32_t rl;
    if (fs->sb.s_feature_incompat & EXT2_FEATURE_INCOMPAT_FILETYPE) {
        const struct ext2_dirent *d = (const struct ext2_dirent *)buf;
        rl = d->rec_len;
    } else {
        const struct ext2_dirent_legacy *l =
            (const struct ext2_dirent_legacy *)buf;
        rl = l->rec_len;
    }
    if (rl < 8 || rl >= fs->block_size)
        return -1;
    uint32_t off = rl;

    /* Verify it is "..", then rewrite the inode field. */
    if (fs->sb.s_feature_incompat & EXT2_FEATURE_INCOMPAT_FILETYPE) {
        struct ext2_dirent *d = (struct ext2_dirent *)(buf + off);
        if (d->name_len != 2 || memcmp(d->name, "..", 2) != 0)
            return -1;
        d->inode = new_parent_ino;
    } else {
        struct ext2_dirent_legacy *l =
            (struct ext2_dirent_legacy *)(buf + off);
        if (l->name_len != 2 || memcmp(l->name, "..", 2) != 0)
            return -1;
        l->inode = new_parent_ino;
    }

    return ext2_write_block(fs, dblk, buf);
}

static int ext2_vfs_rename(void *fs_private, const char *old_path,
                           const char *new_path)
{
    ext2_fs_info_t *fs = (ext2_fs_info_t *)fs_private;
    if (!fs || !old_path || !new_path) return -1;

    char old_parent_path[256];
    char old_name[EXT2_NAME_LEN + 1];
    char new_parent_path[256];
    char new_name[EXT2_NAME_LEN + 1];

    parse_parent_and_name(old_path, old_parent_path, old_name);
    parse_parent_and_name(new_path, new_parent_path, new_name);

    uint32_t old_parent_ino, new_parent_ino;
    struct ext2_inode old_parent_inode, new_parent_inode;

    if (lookup_directory_by_path(fs, old_parent_path, &old_parent_ino,
                                 &old_parent_inode) < 0)
        return -1;
    if (lookup_directory_by_path(fs, new_parent_path, &new_parent_ino,
                                 &new_parent_inode) < 0)
        return -1;

    uint32_t target_ino;
    if (ext2_lookup(fs, &old_parent_inode, old_name, &target_ino) < 0)
        return -1;

    struct ext2_inode target_inode;
    if (ext2_read_inode(fs, target_ino, &target_inode) < 0)
        return -1;

    /* Handle a colliding destination (POSIX rename replaces it). */
    uint32_t existing_ino;
    if (ext2_lookup(fs, &new_parent_inode, new_name, &existing_ino) == 0) {
        struct ext2_inode existing_inode;
        if (ext2_read_inode(fs, existing_ino, &existing_inode) < 0)
            return -1;

        if (EXT2_ISDIR(existing_inode.i_mode)) {
            if (!ext2_dir_is_empty(fs, &existing_inode))
                return -1;
            if (ext2_truncate_file(fs, existing_ino, &existing_inode, 0) < 0)
                return -1;
            if (ext2_write_inode(fs, existing_ino, &existing_inode) < 0)
                return -1;
            if (ext2_free_inode(fs, existing_ino) < 0)
                return -1;
            new_parent_inode.i_links_count--;
        } else {
            if (ext2_truncate_file(fs, existing_ino, &existing_inode, 0) < 0)
                return -1;
            if (ext2_write_inode(fs, existing_ino, &existing_inode) < 0)
                return -1;
            if (ext2_free_inode(fs, existing_ino) < 0)
                return -1;
        }

        if (ext2_remove_dirent(fs, &new_parent_inode, new_name) < 0)
            return -1;
    }

    /* Add to the destination directory. */
    if (ext2_add_dirent(fs, new_parent_ino, &new_parent_inode, new_name,
                        target_ino,
                        ext2_mode_to_ftype(target_inode.i_mode)) < 0)
        return -1;

    /* Remove from the source directory. */
    if (ext2_remove_dirent(fs, &old_parent_inode, old_name) < 0)
        return -1;

    /* Moving a directory between parents: fix ".." and link counts. */
    if (EXT2_ISDIR(target_inode.i_mode) && old_parent_ino != new_parent_ino) {
        if (ext2_update_dotdot(fs, target_ino, &target_inode,
                               new_parent_ino) < 0)
            return -1;
        old_parent_inode.i_links_count--;
        new_parent_inode.i_links_count++;
    }

    if (ext2_write_inode(fs, old_parent_ino, &old_parent_inode) < 0)
        return -1;
    if (ext2_write_inode(fs, new_parent_ino, &new_parent_inode) < 0)
        return -1;

    target_inode.i_ctime = rtc_get_unix_time();
    ext2_write_inode(fs, target_ino, &target_inode);

    ext2_sync_bitmaps(fs);
    return 0;
}

/* ============================================================================
 * VFS Callbacks: link / symlink / readlink
 * ============================================================================ */

static int ext2_vfs_link(void *fs_private, const char *old_path,
                         const char *new_path)
{
    ext2_fs_info_t *fs = (ext2_fs_info_t *)fs_private;
    if (!fs || !old_path || !new_path) return -1;

    /* Resolve the source inode. */
    void *file_private = NULL;
    if (ext2_vfs_open(fs, old_path, O_RDONLY, 0, &file_private) < 0)
        return -1;
    ext2_file_info_t *file = (ext2_file_info_t *)file_private;

    if (EXT2_ISDIR(file->inode.i_mode)) {
        ext2_vfs_close(file_private);
        return -1;   /* hard links to directories are not permitted */
    }

    uint32_t ino = file->inode_num;

    char parent_path[256];
    char name[EXT2_NAME_LEN + 1];
    parse_parent_and_name(new_path, parent_path, name);
    if (name[0] == '\0' || strcmp(name, ".") == 0 ||
        strcmp(name, "..") == 0 || strchr(name, '/') != NULL) {
        ext2_vfs_close(file_private);
        return -1;
    }

    uint32_t parent_ino;
    struct ext2_inode parent_inode;
    if (lookup_directory_by_path(fs, parent_path, &parent_ino,
                                 &parent_inode) < 0) {
        ext2_vfs_close(file_private);
        return -1;
    }

    uint32_t existing;
    if (ext2_lookup(fs, &parent_inode, name, &existing) == 0) {
        ext2_vfs_close(file_private);
        return -1;
    }

    if (ext2_add_dirent(fs, parent_ino, &parent_inode, name, ino,
                        ext2_mode_to_ftype(file->inode.i_mode)) < 0) {
        ext2_vfs_close(file_private);
        return -1;
    }

    file->inode.i_links_count++;
    file->dirty = 1;
    if (ext2_vfs_close(file_private) < 0)
        return -1;

    if (ext2_write_inode(fs, parent_ino, &parent_inode) < 0)
        return -1;

    ext2_sync_bitmaps(fs);
    return 0;
}

static int ext2_vfs_symlink(void *fs_private, const char *target,
                            const char *new_path)
{
    ext2_fs_info_t *fs = (ext2_fs_info_t *)fs_private;
    if (!fs || !target || !new_path) return -1;

    char parent_path[256];
    char name[EXT2_NAME_LEN + 1];
    parse_parent_and_name(new_path, parent_path, name);
    if (name[0] == '\0' || strcmp(name, ".") == 0 ||
        strcmp(name, "..") == 0 || strchr(name, '/') != NULL)
        return -1;

    uint32_t parent_ino;
    struct ext2_inode parent_inode;
    if (lookup_directory_by_path(fs, parent_path, &parent_ino,
                                 &parent_inode) < 0)
        return -1;

    uint32_t existing;
    if (ext2_lookup(fs, &parent_inode, name, &existing) == 0)
        return -1;

    size_t tlen = strlen(target);
    if (tlen > 512)
        return -1;

    /* Allocate a symlink inode. */
    uint32_t ino;
    uint32_t pref_group = (parent_ino - 1) / fs->inodes_per_group;
    if (ext2_alloc_inode(fs, EXT2_S_IFLNK | 0777, pref_group, &ino) < 0)
        return -1;

    struct ext2_inode inode;
    if (ext2_read_inode(fs, ino, &inode) < 0) {
        ext2_free_inode(fs, ino);
        return -1;
    }

    /* Short targets: fast symlink stored in i_block[] (Linux-compatible). */
    if (tlen <= 60) {
        memset(inode.i_block, 0, sizeof(inode.i_block));
        memcpy(inode.i_block, target, tlen);
        inode.i_size = tlen;
        inode.i_blocks = 0;
    } else {
        if (ext2_write_file(fs, ino, &inode, 0, (const uint8_t *)target,
                            (uint32_t)tlen) < 0) {
            ext2_free_inode(fs, ino);
            return -1;
        }
    }

    if (ext2_write_inode(fs, ino, &inode) < 0) {
        ext2_free_inode(fs, ino);
        return -1;
    }

    if (ext2_add_dirent(fs, parent_ino, &parent_inode, name, ino,
                        EXT2_FT_SYMLINK) < 0) {
        ext2_free_inode(fs, ino);
        return -1;
    }

    parent_inode.i_mtime = rtc_get_unix_time();
    if (ext2_write_inode(fs, parent_ino, &parent_inode) < 0)
        return -1;

    ext2_sync_bitmaps(fs);
    return 0;
}

static int ext2_vfs_readlink(void *fs_private, const char *path,
                             char *buf, size_t bufsize)
{
    ext2_fs_info_t *fs = (ext2_fs_info_t *)fs_private;
    if (!fs || !path || !buf || bufsize == 0) return -1;

    char parent_path[256];
    char name[EXT2_NAME_LEN + 1];
    parse_parent_and_name(path, parent_path, name);

    uint32_t parent_ino;
    struct ext2_inode parent_inode;
    if (lookup_directory_by_path(fs, parent_path, &parent_ino,
                                 &parent_inode) < 0)
        return -1;

    uint32_t ino;
    if (ext2_lookup(fs, &parent_inode, name, &ino) < 0)
        return -1;

    struct ext2_inode inode;
    if (ext2_read_inode(fs, ino, &inode) < 0)
        return -1;
    if (!EXT2_ISLNK(inode.i_mode))
        return -1;

    uint32_t len = inode.i_size;
    if (len > bufsize - 1)
        len = bufsize - 1;

    if (inode.i_blocks == 0 && inode.i_size <= 60) {
        /* fast symlink */
        memcpy(buf, inode.i_block, len);
    } else {
        if (ext2_read_file(fs, &inode, 0, (uint8_t *)buf, len) < 0)
            return -1;
    }
    buf[len] = '\0';
    return 0;
}

/* ============================================================================
 * VFS Callbacks: stat / fstat
 * ============================================================================ */

static int ext2_vfs_stat(void *fs_private, const char *path, stat_t *st)
{
    ext2_fs_info_t *fs = (ext2_fs_info_t *)fs_private;
    if (!fs || !path || !st) return -1;

    void *file_private = NULL;
    if (ext2_vfs_open(fs, path, O_RDONLY, 0, &file_private) < 0)
        return -1;
    ext2_file_info_t *file = (ext2_file_info_t *)file_private;

    ext2_fill_stat(&file->inode, file->inode_num, st);
    ext2_vfs_close(file_private);
    return 0;
}

static int ext2_vfs_fstat(void *file_private, stat_t *st)
{
    ext2_file_info_t *file = (ext2_file_info_t *)file_private;
    if (!file || !st) return -1;

    ext2_fill_stat(&file->inode, file->inode_num, st);
    return 0;
}

/* ============================================================================
 * VFS Callbacks: chmod / fchmod
 * ============================================================================ */

static int ext2_vfs_chmod(void *fs_private, const char *path, uint32_t mode)
{
    ext2_fs_info_t *fs = (ext2_fs_info_t *)fs_private;
    if (!fs || !path) return -1;

    void *file_private = NULL;
    if (ext2_vfs_open(fs, path, O_RDONLY, 0, &file_private) < 0)
        return -1;
    ext2_file_info_t *file = (ext2_file_info_t *)file_private;

    file->inode.i_mode = (file->inode.i_mode & EXT2_S_IFMT) | (mode & 07777);
    file->dirty = 1;
    if (ext2_write_inode(fs, file->inode_num, &file->inode) < 0) {
        ext2_vfs_close(file_private);
        return -1;
    }
    ext2_vfs_close(file_private);
    return 0;
}

static int ext2_vfs_fchmod(void *file_private, uint32_t mode)
{
    ext2_file_info_t *file = (ext2_file_info_t *)file_private;
    if (!file) return -1;

    file->inode.i_mode = (file->inode.i_mode & EXT2_S_IFMT) | (mode & 07777);
    file->dirty = 1;
    return ext2_write_inode(file->fs, file->inode_num, &file->inode);
}

/* ============================================================================
 * VFS Callback: mknod (FIFOs and device nodes)
 * ============================================================================ */

static int ext2_vfs_mknod(void *fs_private, const char *path, uint32_t mode,
                          uint32_t dev)
{
    ext2_fs_info_t *fs = (ext2_fs_info_t *)fs_private;
    if (!fs || !path) return -1;

    char parent_path[256];
    char name[EXT2_NAME_LEN + 1];
    parse_parent_and_name(path, parent_path, name);
    if (name[0] == '\0' || strcmp(name, ".") == 0 ||
        strcmp(name, "..") == 0 || strchr(name, '/') != NULL)
        return -1;

    uint32_t parent_ino;
    struct ext2_inode parent_inode;
    if (lookup_directory_by_path(fs, parent_path, &parent_ino,
                                 &parent_inode) < 0)
        return -1;

    uint32_t existing;
    if (ext2_lookup(fs, &parent_inode, name, &existing) == 0)
        return -1;

    /* The syscall passes S_IF* + perms; default to FIFO if no type. */
    uint16_t type = mode & EXT2_S_IFMT;
    if (type == 0)
        type = EXT2_S_IFIFO;
    uint16_t mode_bits = type | (mode & 07777);

    uint32_t ino;
    uint32_t pref_group = (parent_ino - 1) / fs->inodes_per_group;
    if (ext2_alloc_inode(fs, mode_bits, pref_group, &ino) < 0)
        return -1;

    struct ext2_inode inode;
    if (ext2_read_inode(fs, ino, &inode) < 0) {
        ext2_free_inode(fs, ino);
        return -1;
    }

    /* Store the device number in i_block[0] (high bits in i_osd1). */
    if (EXT2_ISCHR(mode_bits) || EXT2_ISBLK(mode_bits)) {
        inode.i_block[0] = dev & 0xFFFF;
        inode.i_osd1 = (dev >> 16) & 0xFFFF;
    }

    if (ext2_write_inode(fs, ino, &inode) < 0) {
        ext2_free_inode(fs, ino);
        return -1;
    }

    if (ext2_add_dirent(fs, parent_ino, &parent_inode, name, ino,
                        ext2_mode_to_ftype(mode_bits)) < 0) {
        ext2_free_inode(fs, ino);
        return -1;
    }

    if (ext2_write_inode(fs, parent_ino, &parent_inode) < 0)
        return -1;

    ext2_sync_bitmaps(fs);
    return 0;
}

/* ============================================================================
 * VFS Callbacks: utime / chown / fchown
 * ============================================================================ */

static int ext2_vfs_utime(void *fs_private, const char *path,
                          uint32_t atime, uint32_t mtime)
{
    ext2_fs_info_t *fs = (ext2_fs_info_t *)fs_private;
    if (!fs || !path) return -1;

    void *file_private = NULL;
    if (ext2_vfs_open(fs, path, O_RDONLY, 0, &file_private) < 0)
        return -1;
    ext2_file_info_t *file = (ext2_file_info_t *)file_private;

    file->inode.i_atime = atime;
    file->inode.i_mtime = mtime;
    file->dirty = 1;
    if (ext2_write_inode(fs, file->inode_num, &file->inode) < 0) {
        ext2_vfs_close(file_private);
        return -1;
    }
    ext2_vfs_close(file_private);
    return 0;
}

static int ext2_vfs_chown(void *fs_private, const char *path, int uid, int gid)
{
    ext2_fs_info_t *fs = (ext2_fs_info_t *)fs_private;
    if (!fs || !path) return -1;

    void *file_private = NULL;
    if (ext2_vfs_open(fs, path, O_RDONLY, 0, &file_private) < 0)
        return -1;
    ext2_file_info_t *file = (ext2_file_info_t *)file_private;

    if (uid >= 0) file->inode.i_uid = (uint16_t)uid;
    if (gid >= 0) file->inode.i_gid = (uint16_t)gid;
    file->dirty = 1;
    if (ext2_write_inode(fs, file->inode_num, &file->inode) < 0) {
        ext2_vfs_close(file_private);
        return -1;
    }
    ext2_vfs_close(file_private);
    return 0;
}

static int ext2_vfs_fchown(void *file_private, int uid, int gid)
{
    ext2_file_info_t *file = (ext2_file_info_t *)file_private;
    if (!file) return -1;

    if (uid >= 0) file->inode.i_uid = (uint16_t)uid;
    if (gid >= 0) file->inode.i_gid = (uint16_t)gid;
    file->dirty = 1;
    return ext2_write_inode(file->fs, file->inode_num, &file->inode);
}

/* ============================================================================
 * VFS Callback: statfs
 * ============================================================================ */

static int ext2_vfs_statfs(void *fs_private, fs_statfs_t *st)
{
    ext2_fs_info_t *fs = (ext2_fs_info_t *)fs_private;
    if (!fs || !st) return -1;

    memset(st, 0, sizeof(*st));
    st->f_type    = EXT2_SUPER_MAGIC;
    st->f_bsize   = fs->block_size;
    st->f_frsize  = fs->block_size;
    st->f_blocks  = fs->sb.s_blocks_count;
    st->f_bfree   = fs->sb.s_free_blocks_count;
    st->f_bavail  = fs->sb.s_free_blocks_count;
    st->f_files   = fs->sb.s_inodes_count;
    st->f_ffree   = fs->sb.s_free_inodes_count;
    st->f_namelen = EXT2_NAME_LEN;
    st->f_fsid    = (uint32_t)((fs->device_id << 16) | fs->partition_id);
    return 0;
}

/* ============================================================================
 * VFS Callback: getpath (resolve an open file back to its path, for fchdir)
 * ============================================================================ */

static int ext2_vfs_getpath(void *file_private, char *buf, size_t bufsize)
{
    ext2_file_info_t *file = (ext2_file_info_t *)file_private;
    if (!file || !buf) return -1;
    ext2_fs_info_t *fs = file->fs;
    uint32_t ino = file->inode_num;

    if (ino == EXT2_ROOT_INO) {
        if (bufsize < 2) return -1;
        strcpy(buf, "/");
        return 0;
    }

    char rev[256];
    int revlen = 0;
    uint8_t blk[4096];
    int depth = 0;

    while (ino != EXT2_ROOT_INO) {
        /* Safety: a corrupted directory tree (e.g. a ".." cycle) must not
         * hang the caller.  The tree depth is bounded by the inode count. */
        if (++depth > (int)fs->sb.s_inodes_count)
            return -1;

        uint32_t parent_ino = 0;
        char child_name[EXT2_NAME_LEN + 1];
        child_name[0] = 0;

        for (uint32_t d = 1; d <= fs->sb.s_inodes_count; d++) {
            if (d == ino) continue;
            struct ext2_inode di;
            if (ext2_read_inode(fs, d, &di) < 0) continue;
            if (!EXT2_ISDIR(di.i_mode)) continue;

            uint32_t nblocks = (di.i_size + fs->block_size - 1) /
                               fs->block_size;
            for (uint32_t b = 0; b < nblocks; b++) {
                uint32_t dblk;
                if (ext2_bmap(fs, &di, b, &dblk) < 0) break;
                if (dblk == 0) continue;
                if (ext2_read_block(fs, dblk, blk) < 0) break;

                uint32_t off = 0;
                while (off + 8 <= fs->block_size) {
                    uint32_t e_ino, rl;
                    uint8_t e_nl;
                    const char *e_nm;
                    if (fs->sb.s_feature_incompat & EXT2_FEATURE_INCOMPAT_FILETYPE) {
                        const struct ext2_dirent *de =
                            (const struct ext2_dirent *)(blk + off);
                        e_ino = de->inode; rl = de->rec_len;
                        e_nl = de->name_len; e_nm = de->name;
                    } else {
                        const struct ext2_dirent_legacy *de =
                            (const struct ext2_dirent_legacy *)(blk + off);
                        e_ino = de->inode; rl = de->rec_len;
                        e_nl = de->name_len; e_nm = de->name;
                    }
                    if (rl < 8 || off + rl > fs->block_size) break;
                    if (e_ino == ino) {
                        /* Ignore "." / ".." self references. */
                        if (e_nl > 0 && e_nm[0] == '.' &&
                            (e_nl == 1 || (e_nl == 2 && e_nm[1] == '.')))
                            goto skip;
                        parent_ino = d;
                        memcpy(child_name, e_nm, e_nl);
                        child_name[e_nl] = 0;
                        goto found;
                    }
                skip:
                    off += rl;
                }
            }
        }
        if (!parent_ino)
            return -1;   /* orphaned entry */
found:
        {
            size_t nlen = strlen(child_name);
            if (nlen == 0 || revlen + (int)nlen + 1 >= (int)sizeof(rev))
                return -1;
            memmove(rev + nlen + 1, rev, (size_t)revlen);
            rev[0] = '/';
            memcpy(rev + 1, child_name, nlen);
            revlen += (int)nlen + 1;
        }
        ino = parent_ino;
    }

    if (revlen == 0) {
        strcpy(buf, "/");
        return 0;
    }
    if ((size_t)revlen + 1 > bufsize)
        return -1;
    memcpy(buf, rev, (size_t)revlen + 1);
    return 0;
}

/* ============================================================================
 * VFS Callback Table
 * ============================================================================ */

static fs_ops_t ext2_ops = {
    .mount    = ext2_mount,
    .unmount  = ext2_unmount,
    .open     = ext2_vfs_open,
    .close    = ext2_vfs_close,
    .read     = ext2_vfs_read,
    .write    = ext2_vfs_write,
    .seek     = ext2_vfs_seek,
    .truncate = ext2_vfs_truncate,
    .readdir  = ext2_vfs_readdir,
    .mkdir    = ext2_vfs_mkdir,
    .rmdir    = ext2_vfs_rmdir,
    .unlink   = ext2_vfs_unlink,
    .rename   = ext2_vfs_rename,
    .stat     = ext2_vfs_stat,
    .fstat    = ext2_vfs_fstat,
    .link     = ext2_vfs_link,
    .symlink  = ext2_vfs_symlink,
    .readlink = ext2_vfs_readlink,
    .chmod    = ext2_vfs_chmod,
    .fchmod   = ext2_vfs_fchmod,
    .mknod    = ext2_vfs_mknod,
    .utime    = ext2_vfs_utime,
    .chown    = ext2_vfs_chown,
    .fchown   = ext2_vfs_fchown,
    .statfs   = ext2_vfs_statfs,
    .getpath  = ext2_vfs_getpath,
};

/* ============================================================================
 * Initialisation
 * ============================================================================ */

void ext2_init(void)
{
    if (register_filesystem("ext2", &ext2_ops) < 0) {
        printk("[ext2] FAILED to register filesystem type\n");
        return;
    }
    printk("[ext2] ext2 filesystem driver registered\n");
}

/* ============================================================================
 * Path Helpers
 * ============================================================================ */

/**
 * Split a path into its parent directory path and final component name.
 * For "a/b/c" -> parent="a/b", name="c"; for "/x" -> parent="/", name="x".
 */
static int parse_parent_and_name(const char *path, char *parent, char *name)
{
    if (!path || !parent || !name) return -1;
    if (path[0] == '\0') return -1;

    const char *slash = NULL;
    for (const char *p = path; *p; p++) {
        if (*p == '/')
            slash = p;
    }

    if (!slash) {
        strcpy(parent, "/");
        strcpy(name, path);
        return 0;
    }

    if (slash == path) {
        strcpy(parent, "/");
    } else {
        size_t len = (size_t)(slash - path);
        memcpy(parent, path, len);
        parent[len] = '\0';
    }

    strcpy(name, slash + 1);
    return 0;
}

/**
 * Resolve a (relative-to-mount-root) path to a directory inode.
 */
static int lookup_directory_by_path(ext2_fs_info_t *fs, const char *path,
                                    uint32_t *ino_out,
                                    struct ext2_inode *inode_out)
{
    if (!fs || !path || !ino_out || !inode_out) return -1;

    if (path[0] == '\0' || strcmp(path, "/") == 0 ||
        strcmp(path, ".") == 0) {
        *ino_out = EXT2_ROOT_INO;
        return ext2_read_inode(fs, EXT2_ROOT_INO, inode_out);
    }

    const char *p = path;
    if (*p == '/') p++;

    uint32_t current_ino = EXT2_ROOT_INO;
    struct ext2_inode dir_inode;
    if (ext2_read_inode(fs, current_ino, &dir_inode) < 0)
        return -1;

    char path_copy[256];
    strncpy(path_copy, p, sizeof(path_copy) - 1);
    path_copy[sizeof(path_copy) - 1] = '\0';

    char *token = path_copy;
    char *next = token;
    while (*token) {
        while (*next && *next != '/') next++;
        int is_last = (*next == '\0');
        *next = '\0';

        if (token[0] == '\0') {
            if (!is_last) {
                token = next + 1;
                next = token;
            }
            continue;
        }

        if (ext2_lookup(fs, &dir_inode, token, &current_ino) < 0)
            return -1;
        if (ext2_read_inode(fs, current_ino, &dir_inode) < 0)
            return -1;
        if (!is_last && !EXT2_ISDIR(dir_inode.i_mode))
            return -1;

        if (is_last) break;
        token = next + 1;
        next = token;
    }

    *ino_out = current_ino;
    *inode_out = dir_inode;
    return 0;
}
