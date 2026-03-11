#ifndef DEVFS_H
#define DEVFS_H

#include <stdint.h>
#include "fs/fs.h"   /* DT_BLKDEV, DT_CHRDEV */

/* =========================================================================
 * devfs – in-memory device filesystem
 *
 * A flat, RAM-only filesystem that exposes registered hardware devices as
 * files under /dev.  Only block and character device files are supported;
 * no subdirectories, no regular files, no symlinks.
 *
 * Lifecycle
 * ---------
 * 1. Drivers call devfs_register_device() at any point (even before
 *    devfs_init() runs) to add their node to the static table.
 * 2. kernel_main() calls devfs_init() after vfs_init().  devfs_init()
 *    registers the "devfs" filesystem type with the VFS and mounts it
 *    at /dev.
 * 3. From that point on, user code can open /dev/hda, /dev/tty0, etc.
 * ========================================================================= */

#define DEVFS_MAX_NODES  64   /* maximum number of device nodes */

/* -------------------------------------------------------------------------
 * Device node descriptor
 * ------------------------------------------------------------------------- */

typedef struct devfs_node {
    char     name[64];   /* e.g. "hda", "tty0" – no leading slash */
    uint8_t  type;       /* DT_BLKDEV or DT_CHRDEV                */
    int      dev_id;     /* primary device ID (prim_id in driver)  */
    int      minor;      /* secondary device ID (scnd_id)          */
    int      in_use;     /* 1 = slot occupied                      */
} devfs_node_t;

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

/**
 * Register a device node in the devfs node table.
 *
 * May be called before devfs_init() – nodes are stored in a static array
 * and become visible the moment devfs is mounted.
 *
 * @param name   Short device name, e.g. "hda", "tty0".  Must be < 64 chars.
 * @param type   DT_BLKDEV or DT_CHRDEV.
 * @param dev_id Primary device ID (prim_id used with bread/bwrite/cread/cwrite).
 * @param minor  Secondary device ID (scnd_id).
 * @return 0 on success, -1 if the table is full or name already exists.
 */
int devfs_register_device(const char *name, uint8_t type,
                          int dev_id, int minor);

/**
 * Remove a device node from the devfs node table.
 *
 * @param name  The name that was used in devfs_register_device().
 * @return 0 on success, -1 if not found.
 */
int devfs_unregister_device(const char *name);

/**
 * Initialise devfs: register the "devfs" filesystem type with the VFS and
 * mount it at /dev.  Must be called after vfs_init().
 */
void devfs_init(void);

#endif /* DEVFS_H */