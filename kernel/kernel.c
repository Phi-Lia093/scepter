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
#include "mm/buddy.h"
#include "mm/slab.h"
#include "driver/block/cache.h"
#include "fs/fs.h"
#include "fs/devfs.h"
#include "asm.h"

/* =========================================================================
 * Linker-provided symbol: one byte past the end of the kernel image.
 * Declared as a zero-length array so &kernel_end gives the address.
 * The physical address is (uint32_t)&kernel_end - KERNEL_VMA.
 * ========================================================================= */
extern char kernel_end[];
#define KERNEL_VMA  0xC0000000U

/* =========================================================================
 * Global memory information (set once in kernel_main, read-only after that)
 * ========================================================================= */

/* Total detected RAM in kilobytes (from CMOS detection) */
uint32_t mem_total_kb  = 0;

/* Physical address of the first 4 KB-aligned page that is free to use
 * (i.e. the page immediately after the kernel image). */
uint32_t mem_first_free_phys = 0;

/* =========================================================================
 * CMOS Memory Detection
 * ========================================================================= */

static uint32_t detect_memory_cmos(void)
{
    /* Extended memory (1 MB – 16 MB) from CMOS registers 0x17-0x18 */
    outb(0x70, 0x17);
    uint32_t low = inb(0x71);
    outb(0x70, 0x18);
    uint32_t high = inb(0x71);
    uint32_t extended_kb = (high << 8) | low;

    /* Memory above 16 MB from CMOS registers 0x34-0x35 (64 KB blocks) */
    outb(0x70, 0x34);
    low = inb(0x71);
    outb(0x70, 0x35);
    high = inb(0x71);
    uint32_t above_16mb_kb = ((high << 8) | low) * 64;

    return 1024 + extended_kb + above_16mb_kb;
}

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
     * Memory detection and allocators
     * ------------------------------------------------------------------ */
    mem_total_kb = detect_memory_cmos();
    printk("[MEM] Detected %u KB (%u MB) via CMOS\n",
           mem_total_kb, mem_total_kb / 1024);

    uint32_t kernel_end_phys = (uint32_t)kernel_end - KERNEL_VMA;
    mem_first_free_phys = (kernel_end_phys + 0xFFF) & ~0xFFFU;

    printk("[MEM] kernel image end: phys=0x%08x  virt=0x%08x\n",
           kernel_end_phys, (uint32_t)kernel_end);
    printk("[MEM] first free page:  phys=0x%08x\n\n",
           mem_first_free_phys);

    uint32_t max_phys_mapped   = 0x40000000U;
    uint32_t max_phys_detected = mem_total_kb * 1024;
    uint32_t max_phys = (max_phys_detected < max_phys_mapped)
                        ? max_phys_detected : max_phys_mapped;

    uint32_t buddy_mem_kb = (max_phys - mem_first_free_phys) / 1024;

    printk("[MEM] Pre-mapped region: phys 0x00000000-0x3FFFFFFF (1 GB)\n");
    printk("[MEM] Detected RAM:      %u KB (%u MB)\n",
           mem_total_kb, mem_total_kb / 1024);
    printk("[MEM] Usable limit:      phys 0x%08x (%u MB)\n",
           max_phys, max_phys / (1024 * 1024));
    printk("[MEM] Low memory 0-1MB:  RESERVED\n");
    printk("[MEM] Buddy range:       phys 0x%08x-0x%08x\n",
           mem_first_free_phys, max_phys);
    printk("[MEM] Buddy memory:      %u KB (%u MB)\n",
           buddy_mem_kb, buddy_mem_kb / 1024);

    buddy_init(mem_first_free_phys, buddy_mem_kb);
    slab_init();

    /* ------------------------------------------------------------------
     * Kernel services
     * ------------------------------------------------------------------ */
    sched_init();
    cache_init();

    /* ------------------------------------------------------------------
     * Character drivers  (each registers itself + devfs node internally)
     * vga_init() was already called above for early printk; tty/pit/kbd
     * are safe to init here after memory is set up.
     * ------------------------------------------------------------------ */
    tty_init();
    pit_init(100);
    kbd_init();

    printk("Early initialization complete.\n\n");

    /* ------------------------------------------------------------------
     * Block drivers + partition scan  (ide, mbr — all self-registering)
     * ------------------------------------------------------------------ */
    block_init();

    /* ------------------------------------------------------------------
     * Virtual filesystem
     * ------------------------------------------------------------------ */
    vfs_init();
    devfs_init();   /* mount devfs at /dev */

    printk("\nKernel initialization complete.\n\n");

    /* Quick sanity-check: read the last byte of the first IDE sector */
    int fd = fs_open("/dev/hda", 1);
    if (fd >= 0) {
        char buf[512];
        fs_read(fd, buf, 512);
        printk("[TEST] hda sector 0 byte 511 = 0x%02x\n", (unsigned char)buf[511]);
    }

    sti();
    while (1);
}