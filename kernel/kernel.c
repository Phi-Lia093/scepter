#include "kernel/cpu.h"
#include "kernel/sched.h"
#include "driver/char/vga.h"
#include "driver/char/tty.h"
#include "driver/char/pit.h"
#include "driver/char/kbd.h"
#include "driver/char/serial.h"
#include "driver/char/char.h"
#include "driver/block/block.h"
#include "driver/block/ide.h"
#include "driver/block/part_mbr.h"
#include "driver/pic.h"
#include "lib/printk.h"
#include "lib/string.h"
#include "mm/mm.h"
#include "mm/slab.h"
#include "driver/block/cache.h"
#include "fs/fs.h"
#include "fs/devfs.h"
#include "fs/minix3.h"

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

    /* VGA and serial for early output */
    vga_init();
    serial_init();  /* Initialize serial port early for logging */

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
    minix3_init();  /* register minix3 filesystem driver */

    printk("[KERNEL] Initialization complete\n\n");

    /* ------------------------------------------------------------------
     * Mount root filesystem
     * ------------------------------------------------------------------ */
    if (fs_mount(5, 1, "minix3", "/") != 0) {
        printk("[KERNEL] Failed to mount root filesystem\n");
        sti();
        while (1);
    }

    int fd = fs_open("/test.txt", O_CREAT | O_RDWR);
    fs_write(fd, "hello", 6);
    fs_close(fd);
    sti();
    while (1);
}
