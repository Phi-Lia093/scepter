#ifndef FS_PROCFS_H
#define FS_PROCFS_H

/* =========================================================================
 * procfs - a pseudo filesystem exposing kernel/systems statistics.
 *
 * Registers the "procfs" filesystem type.  It is mounted by init(1) at
 * /proc (like devfs is mounted at /dev).
 * ========================================================================= */

/** Register the procfs filesystem driver (called at boot). */
void procfs_init(void);

#endif /* FS_PROCFS_H */
