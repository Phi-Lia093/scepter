/* ============================================================================
 * ext2 Filesystem - Directory Operations
 * ============================================================================ */

#include "fs/ext2.h"
#include "fs/fs.h"
#include "driver/block/block.h"
#include "driver/char/rtc.h"
#include "mm/slab.h"
#include "lib/printk.h"
#include "lib/string.h"

/* ============================================================================
 * Directory entry helpers
 *
 * Directory blocks hold a packed chain of variable-length entries linked by
 * rec_len (a multiple of 4).  The chain within each block always sums to the
 * block size.  Entries with inode == 0 are unused/deleted.
 *
 * With EXT2_FEATURE_INCOMPAT_FILETYPE each entry carries an extra file_type
 * byte; otherwise the legacy 3-byte header form is used.
 * ============================================================================ */

static inline int ext2_has_filetype(ext2_fs_info_t *fs)
{
    return (fs->sb.s_feature_incompat & EXT2_FEATURE_INCOMPAT_FILETYPE) != 0;
}

/* rec_len needed to store an entry with a name of nlen bytes (4-aligned).
 * Both dirent formats have an 8-byte header: {u32 inode, u16 rec_len, u8
 * name_len, u8 file_type} or the legacy {u32 inode, u16 rec_len, u16
 * name_len}. */
static uint16_t ext2_rec_len(ext2_fs_info_t *fs, uint32_t nlen)
{
    (void)fs;
    return (uint16_t)(((nlen + 8) + 3) & ~3u);
}

/* Write one entry at p with the given fields.  The area after the name
 * (up to rec_len) is left untouched. */
static void ext2_write_dirent(ext2_fs_info_t *fs, void *p, uint32_t ino,
                              const char *name, uint32_t nlen,
                              uint8_t ftype, uint16_t rec_len)
{
    if (ext2_has_filetype(fs)) {
        struct ext2_dirent *d = (struct ext2_dirent *)p;
        d->inode = ino;
        d->rec_len = rec_len;
        d->name_len = (uint8_t)nlen;
        d->file_type = ftype;
        memcpy(d->name, name, nlen);
    } else {
        struct ext2_dirent_legacy *l = (struct ext2_dirent_legacy *)p;
        l->inode = ino;
        l->rec_len = rec_len;
        l->name_len = (uint8_t)nlen;
        memcpy(l->name, name, nlen);
    }
}

/* Read the fields of the entry at p (format-aware). */
static void ext2_read_dirent(ext2_fs_info_t *fs, const void *p,
                             uint32_t *ino, uint16_t *rec_len,
                             uint8_t *nlen, uint8_t *ftype,
                             const char **name)
{
    if (ext2_has_filetype(fs)) {
        const struct ext2_dirent *d = (const struct ext2_dirent *)p;
        *ino = d->inode;
        *rec_len = d->rec_len;
        *nlen = d->name_len;
        *ftype = d->file_type;
        *name = d->name;
    } else {
        const struct ext2_dirent_legacy *l =
            (const struct ext2_dirent_legacy *)p;
        *ino = l->inode;
        *rec_len = l->rec_len;
        *nlen = l->name_len;
        *ftype = EXT2_FT_UNKNOWN;
        *name = l->name;
    }
}

/* ============================================================================
 * Directory de-indexing
 *
 * When a filesystem is created with the dir_index feature the directories
 * contain an htree index rooted in block 0.  The '.' entry there has
 * rec_len 12 and the '..' entry has rec_len block_size - 12, with the index
 * data hidden inside the '..' rec_len span.  We do not implement htree
 * maintenance, so before a directory is modified we convert block 0 back to
 * a plain directory block and clear the feature bit (once) - the leaf blocks
 * hold all real entries and stay untouched, so this is a safe downgrade.
 * ============================================================================ */

static int ext2_deindex_dir(ext2_fs_info_t *fs, uint32_t dir_ino,
                            struct ext2_inode *dir, uint8_t *buf)
{
    if (!(fs->sb.s_feature_compat & EXT2_FEATURE_COMPAT_DIR_INDEX))
        return 0;   /* nothing indexed */

    uint32_t dblk;

    if (ext2_bmap(fs, dir, 0, &dblk) < 0 || dblk == 0)
        return 0;   /* no block 0 - nothing to convert */

    if (ext2_read_block(fs, dblk, buf) < 0)
        return -1;

    /* Only convert when block 0 really looks like an htree index root:
     *   '.'  entry with rec_len 12, then
     *   '..' entry with rec_len block_size - 12 (its span hides the index),
     *   and non-zero dx_root data right after the two entries.
     * Plain directories - where '.' and '..' each have rec_len 12 and the
     * real entries follow at offset 24 - are left completely untouched.
     *
     * Two dx_root layouts exist in the wild:
     *   - classic: a full duplicate of '.' / '..' (dot_inode != 0 at 24)
     *   - modern:  8-byte header { reserved, hash_version, info_length,
     *              indirect_levels, unused_flags } (reserved == 0 at 24,
     *              hash_version/info_length non-zero at 28/29)
     * In both cases at least one byte in [24, 32) is non-zero. */
    {
        const struct ext2_dirent *dot = (const struct ext2_dirent *)buf;
        if (dot->rec_len != 12 || dot->name_len != 1 ||
            memcmp(dot->name, ".", 1) != 0)
            return 0;

        const struct ext2_dirent *dotdot =
            (const struct ext2_dirent *)(buf + 12);
        if (dotdot->name_len != 2 || memcmp(dotdot->name, "..", 2) != 0)
            return 0;
        if (dotdot->rec_len != fs->block_size - 12)
            return 0;   /* plain directory layout */

        int indexed = 0;
        for (int i = 24; i < 32; i++)
            if (buf[i]) { indexed = 1; break; }
        if (!indexed)
            return 0;   /* free padding, not an index */
    }

    uint32_t dotdot_ino;
    memcpy(&dotdot_ino, buf + 12, 4);

    /* Rewrite block 0: '.', '..', then one big free slot. */
    memset(buf, 0, fs->block_size);
    uint16_t r1 = ext2_rec_len(fs, 1);
    uint16_t r2 = ext2_rec_len(fs, 2);
    ext2_write_dirent(fs, buf, dir_ino, ".", 1, EXT2_FT_DIR, r1);
    ext2_write_dirent(fs, buf + r1, dotdot_ino, "..", 2, EXT2_FT_DIR, r2);
    if (fs->block_size > r1 + r2) {
        /* one free slot covering the rest of the block */
        ext2_write_dirent(fs, buf + r1 + r2, 0, "", 0, EXT2_FT_UNKNOWN,
                          (uint16_t)(fs->block_size - r1 - r2));
    }

    if (ext2_write_block(fs, dblk, buf) < 0)
        return -1;

    /* Clear the feature (persisted on next sync / unmount). */
    fs->sb.s_feature_compat &= ~EXT2_FEATURE_COMPAT_DIR_INDEX;
    fs->sb_dirty = 1;
    fs->dir_index_pending = 1;
    printk("[ext2] De-indexed directory (inode %u)\n", dir_ino);
    return 0;
}

/* ============================================================================
 * Directory initialisation (mkdir)
 * ============================================================================ */

/**
 * Initialise a freshly allocated directory: write '.' and '..' into its
 * (already allocated) first data block.  The caller sets dir->i_block[0],
 * i_size, i_links_count, i_blocks before calling.
 */
int ext2_init_dir(ext2_fs_info_t *fs, struct ext2_inode *dir,
                  uint32_t dir_ino, uint32_t parent_ino)
{
    if (dir->i_block[0] == 0)
        return -1;

    uint8_t buf[4096];
    memset(buf, 0, fs->block_size);

    uint16_t r1 = ext2_rec_len(fs, 1);
    uint16_t r2 = ext2_rec_len(fs, 2);
    ext2_write_dirent(fs, buf, dir_ino, ".", 1, EXT2_FT_DIR, r1);
    ext2_write_dirent(fs, buf + r1, parent_ino, "..", 2, EXT2_FT_DIR, r2);
    if (fs->block_size > r1 + r2) {
        ext2_write_dirent(fs, buf + r1 + r2, 0, "", 0, EXT2_FT_UNKNOWN,
                          (uint16_t)(fs->block_size - r1 - r2));
    }

    return ext2_write_block(fs, dir->i_block[0], buf);
}

/* ============================================================================
 * Lookup
 * ============================================================================ */

/**
 * Find `name` in the directory `dir`.  Linear scan (works for both plain
 * and indexed directories, since indexed dirs still contain every entry).
 */
int ext2_lookup(ext2_fs_info_t *fs, struct ext2_inode *dir,
                const char *name, uint32_t *ino_out)
{
    if (!EXT2_ISDIR(dir->i_mode))
        return -1;

    uint32_t nlen = strlen(name);
    if (nlen == 0 || nlen > EXT2_NAME_LEN)
        return -1;

    uint32_t num_blocks = (dir->i_size + fs->block_size - 1) / fs->block_size;
    uint8_t buf[4096];

    for (uint32_t b = 0; b < num_blocks; b++) {
        uint32_t dblk;
        if (ext2_bmap(fs, dir, b, &dblk) < 0)
            return -1;
        if (dblk == 0)
            continue;   /* sparse block */

        if (ext2_read_block(fs, dblk, buf) < 0)
            return -1;

        uint32_t off = 0;
        while (off + 8 <= fs->block_size) {
            uint32_t ino;
            uint16_t rl;
            uint8_t nl, ft;
            const char *nm;
            ext2_read_dirent(fs, buf + off, &ino, &rl, &nl, &ft, &nm);
            if (rl < 8 || off + rl > fs->block_size) {
                printk("[ext2] Corrupt dirent in dir inode %u (off %u, rec_len %u)\n",
                       dir->i_mode, off, rl);
                return -1;
            }
            if (ino != 0 && nl == nlen && memcmp(nm, name, nlen) == 0) {
                *ino_out = ino;
                return 0;
            }
            off += rl;
        }
    }

    return -1;   /* not found */
}

/* ============================================================================
 * Add directory entry
 * ============================================================================ */

/**
 * Add an entry to `dir`.  Scans existing blocks for a usable free slot
 * (deleted entry or free tail); splits the slot if there is room, otherwise
 * allocates a new block for the directory.
 *
 * On success `dir` is updated in place (i_size grows, mtime/ctime set);
 * the caller must write the directory inode back to disk.
 */
int ext2_add_dirent(ext2_fs_info_t *fs, uint32_t dir_ino,
                    struct ext2_inode *dir, const char *name,
                    uint32_t ino, uint8_t ftype)
{
    if (!EXT2_ISDIR(dir->i_mode))
        return -1;

    uint32_t nlen = strlen(name);
    if (nlen == 0 || nlen > EXT2_NAME_LEN)
        return -1;

    /* Convert an htree-indexed directory to plain form first.  The caller's
     * buffer is reused so we do not stack two 4 KB block buffers (the kernel
     * stack is only 16 KB and minix3 already uses ~8 KB under syscalls). */
    uint8_t buf[4096];
    if (ext2_deindex_dir(fs, dir_ino, dir, buf) < 0)
        return -1;

    uint32_t entry_size = ext2_rec_len(fs, nlen);

    uint32_t num_blocks = (dir->i_size + fs->block_size - 1) / fs->block_size;

    for (uint32_t b = 0; b < num_blocks; b++) {
        uint32_t dblk;
        if (ext2_bmap(fs, dir, b, &dblk) < 0)
            return -1;
        if (dblk == 0)
            continue;   /* sparse block - skip */

        if (ext2_read_block(fs, dblk, buf) < 0)
            return -1;

        uint32_t off = 0;
        while (off + 8 <= fs->block_size) {
            uint32_t e_ino;
            uint16_t rl;
            uint8_t e_nl, e_ft;
            const char *e_nm;
            ext2_read_dirent(fs, buf + off, &e_ino, &rl, &e_nl, &e_ft, &e_nm);
            if (rl < 8 || off + rl > fs->block_size) {
                printk("[ext2] Corrupt dirent in dir inode %u (off %u, rec_len %u)\n",
                       dir->i_mode, off, rl);
                return -1;
            }

            if (e_ino == 0 && rl >= entry_size) {
                /* Free slot big enough.  Split if we can leave >= 8 bytes. */
                if (rl >= entry_size + 8) {
                    ext2_write_dirent(fs, buf + off + entry_size, 0, "", 0,
                                      EXT2_FT_UNKNOWN,
                                      (uint16_t)(rl - entry_size));
                    rl = entry_size;
                }
                ext2_write_dirent(fs, buf + off, ino, name, nlen, ftype, rl);
                if (ext2_write_block(fs, dblk, buf) < 0)
                    return -1;
                goto added;
            }

            off += rl;
        }
    }

    /* No room in existing blocks: grow the directory by one block. */
    {
        uint32_t dblk;
        if (ext2_bmap_alloc(fs, dir_ino, dir, num_blocks, &dblk) < 0)
            return -1;
        memset(buf, 0, fs->block_size);
        ext2_write_dirent(fs, buf, ino, name, nlen, ftype,
                          (uint16_t)fs->block_size);
        if (ext2_write_block(fs, dblk, buf) < 0)
            return -1;
        dir->i_size = (num_blocks + 1) * fs->block_size;
    }

added:
    /* The directory may have grown; keep i_blocks accurate. */
    ext2_refresh_blocks(fs, dir);
    dir->i_mtime = rtc_get_unix_time();
    dir->i_ctime = dir->i_mtime;
    return 0;
}

/* ============================================================================
 * Remove directory entry
 * ============================================================================ */

/**
 * Remove `name` from `dir`.  The freed slot is merged with any adjacent
 * deleted slot so the space becomes reusable.  The chain invariant (rec_lens
 * sum to block size) is preserved.  Caller must write the dir inode back.
 */
int ext2_remove_dirent(ext2_fs_info_t *fs, struct ext2_inode *dir,
                       const char *name)
{
    if (!EXT2_ISDIR(dir->i_mode))
        return -1;

    uint32_t nlen = strlen(name);
    if (nlen == 0 || nlen > EXT2_NAME_LEN)
        return -1;

    uint8_t buf[4096];
    uint32_t num_blocks = (dir->i_size + fs->block_size - 1) / fs->block_size;

    for (uint32_t b = 0; b < num_blocks; b++) {
        uint32_t dblk;
        if (ext2_bmap(fs, dir, b, &dblk) < 0)
            return -1;
        if (dblk == 0)
            continue;

        if (ext2_read_block(fs, dblk, buf) < 0)
            return -1;

        uint32_t off = 0;
        uint32_t prev_off = 0;
        int have_prev = 0;
        int found = 0;

        while (off + 8 <= fs->block_size) {
            uint32_t e_ino;
            uint16_t rl;
            uint8_t e_nl, e_ft;
            const char *e_nm;
            ext2_read_dirent(fs, buf + off, &e_ino, &rl, &e_nl, &e_ft, &e_nm);
            if (rl < 8 || off + rl > fs->block_size) {
                printk("[ext2] Corrupt dirent in dir inode %u (off %u, rec_len %u)\n",
                       dir->i_mode, off, rl);
                return -1;
            }

            if (e_ino != 0 && e_nl == nlen && memcmp(e_nm, name, nlen) == 0) {
                uint32_t next_off = off + rl;
                int is_last = (next_off >= fs->block_size);

                /* Delete the entry. */
                ext2_write_dirent(fs, buf + off, 0, "", 0, EXT2_FT_UNKNOWN, rl);

                if (!is_last) {
                    /* Merge with a following deleted entry if present. */
                    uint32_t de_ino;
                    uint16_t de_rl;
                    uint8_t de_nl, de_ft;
                    const char *de_nm;
                    ext2_read_dirent(fs, buf + next_off, &de_ino, &de_rl,
                                     &de_nl, &de_ft, &de_nm);
                    if (de_ino == 0 && next_off + de_rl <= fs->block_size) {
                        struct ext2_dirent *d =
                            (struct ext2_dirent *)(buf + off);
                        d->rec_len = (uint16_t)(rl + de_rl);
                    }
                } else if (have_prev) {
                    /* Last entry: absorb into previous if it is deleted. */
                    uint32_t p_ino;
                    uint16_t p_rl;
                    uint8_t p_nl, p_ft;
                    const char *p_nm;
                    ext2_read_dirent(fs, buf + prev_off, &p_ino, &p_rl,
                                     &p_nl, &p_ft, &p_nm);
                    if (p_ino == 0) {
                        struct ext2_dirent *pd =
                            (struct ext2_dirent *)(buf + prev_off);
                        pd->rec_len = (uint16_t)(p_rl + rl);
                    }
                }

                found = 1;
                break;
            }

            prev_off = off;
            have_prev = 1;
            off += rl;
        }

        if (found) {
            if (ext2_write_block(fs, dblk, buf) < 0)
                return -1;
            dir->i_mtime = rtc_get_unix_time();
            dir->i_ctime = dir->i_mtime;
            return 0;
        }
    }

    return -1;   /* name not found */
}
