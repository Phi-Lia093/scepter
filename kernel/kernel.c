#include "kernel/cpu.h"
#include "kernel/sched.h"
#include "driver/char/vga.h"
#include "driver/char/tty.h"
#include "driver/char/pit.h"
#include "driver/char/kbd.h"
#include "driver/char/char.h"
#include "driver/block/block.h"
#include "driver/block/ide.h"
#include "driver/block/part_mbr.h"
#include "driver/pic.h"
#include "lib/printk.h"
#include "mm/mm.h"
#include "driver/block/cache.h"
#include "fs/fs.h"
#include "fs/devfs.h"


/* =========================================================================
 * kernel_main
 * ========================================================================= */

void kernel_main(void)
{
    /* ------------------------------------------------------------------
     * CPU / interrupt infrastructure
     * ------------------------------------------------------------------ */
    gdt_init();
    idt_init();
    isr_init();
    pic_init(0x20, 0x28);

    /* VGA must come first so printk has somewhere to write */
    vga_init();

    /* ------------------------------------------------------------------
     * Memory management (detects RAM, initializes buddy & slab)
     * ------------------------------------------------------------------ */
    mm_init();

    /* ------------------------------------------------------------------
     * Kernel services
     * ------------------------------------------------------------------ */
    sched_init();
    cache_init();

    /* ------------------------------------------------------------------
     * Character drivers  (each registers itself + devfs node internally)
     * ------------------------------------------------------------------ */
    tty_init();
    pit_init(100);
    kbd_init();

    printk("[KERNEL] Early initialization complete\n\n");

    /* ------------------------------------------------------------------
     * Block drivers + partition scan
     * ------------------------------------------------------------------ */
    block_init();

    /* ------------------------------------------------------------------
     * Virtual filesystem
     * ------------------------------------------------------------------ */
    vfs_init();
    devfs_init();   /* mount devfs at /dev */

    printk("[KERNEL] Initialization complete\n\n");

    sti();
    while (1);
}
