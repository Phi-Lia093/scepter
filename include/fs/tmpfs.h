#ifndef FS_TMPFS_H
#define FS_TMPFS_H

/* =========================================================================
 * tmpfs - an in-memory filesystem.
 *
 * Files and directories live entirely in kernel memory (slab/buddy pages).
 * There is no backing block device; content is lost on unmount/reboot.
 * Registers the "tmpfs" filesystem type; init(1) mounts it at /tmp.
 * ========================================================================= */

/** Register the tmpfs filesystem driver (called at boot). */
void tmpfs_init(void);

#endif /* FS_TMPFS_H */
