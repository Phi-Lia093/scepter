/* ============================================================================
 * Minix v3 Filesystem Driver - Main Initialization
 * ============================================================================ */

#include "fs/minix3.h"
#include "fs/fs.h"
#include "lib/printk.h"

/* ============================================================================
 * Filesystem Operations Table
 * ============================================================================ */

static fs_ops_t minix3_ops = {
    .mount    = minix3_mount,
    .unmount  = minix3_unmount,
    .open     = NULL,  /* TODO: Implement */
    .close    = NULL,
    .read     = NULL,
    .write    = NULL,
    .seek     = NULL,
    .truncate = NULL,
    .readdir  = NULL,
    .mkdir    = NULL,
    .rmdir    = NULL,
    .unlink   = NULL,
    .rename   = NULL,
    .stat     = NULL,
};

/* ============================================================================
 * Initialization
 * ============================================================================ */

void minix3_init(void)
{
    if (register_filesystem("minix3", &minix3_ops) < 0) {
        printk("[minix3] Failed to register filesystem\n");
        return;
    }
    
    printk("[minix3] Minix v3 filesystem driver registered\n");
}