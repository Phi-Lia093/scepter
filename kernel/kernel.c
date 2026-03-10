#include "kernel/cpu.h"
#include "driver/char/vga.h"
#include "driver/char/tty.h"
#include "lib/printk.h"
#include "driver/pic.h"
#include "driver/char/pit.h"
#include "mm/buddy.h"
#include "mm/slab.h"
#include "driver/driver.h"
#include "driver/block/cache.h"
#include "driver/char/kbd.h"
#include "driver/block/ide.h"
#include "driver/block/part_mbr.h"
#include "fs/fs.h"
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
    /* Read memory size from CMOS/RTC NVRAM registers */
    
    /* Extended memory (1MB to 16MB) from CMOS registers 0x17-0x18 */
    outb(0x70, 0x17);
    uint32_t low = inb(0x71);
    outb(0x70, 0x18);
    uint32_t high = inb(0x71);
    uint32_t extended_kb = (high << 8) | low;
    
    /* Memory above 16MB from CMOS registers 0x34-0x35 (in 64KB blocks) */
    outb(0x70, 0x34);
    low = inb(0x71);
    outb(0x70, 0x35);
    high = inb(0x71);
    uint32_t above_16mb_kb = ((high << 8) | low) * 64;
    
    /* Total: base 1MB + extended + above_16mb */
    return 1024 + extended_kb + above_16mb_kb;
}

/* =========================================================================
 * kernel_main
 * ========================================================================= */

void kernel_main(void)
{
    gdt_init();
    idt_init();
    isr_init();
    pic_init(0x20, 0x28);
    
    /* Initialize VGA hardware and clear screen FIRST */
    vga_init();

    /* ------------------------------------------------------------------
     * Detect memory size via CMOS
     * ------------------------------------------------------------------ */
    mem_total_kb = detect_memory_cmos();
    printk("[MEM] Detected %u KB (%u MB) via CMOS\n",
           mem_total_kb, mem_total_kb / 1024);

    /* ------------------------------------------------------------------
     * End of kernel image → first free physical page
     *
     * kernel_end is a virtual address in the higher half.
     * Subtract KERNEL_VMA to get the physical address, then round up
     * to the next 4 KB page boundary.
     * ------------------------------------------------------------------ */
    uint32_t kernel_end_phys = (uint32_t)kernel_end - KERNEL_VMA;
    mem_first_free_phys = (kernel_end_phys + 0xFFF) & ~0xFFFU;  /* page-align up */

    printk("[MEM] kernel image end: phys=0x%08x  virt=0x%08x\n",
           kernel_end_phys, (uint32_t)kernel_end);
    printk("[MEM] first free page:  phys=0x%08x\n\n",
           mem_first_free_phys);

    /* ------------------------------------------------------------------
     * All physical memory (0-1GB) is pre-mapped by boot.s
     * Virtual 0xC0000000-0xFFFFFFFF → Physical 0x00000000-0x3FFFFFFF
     *
     * Initialize buddy allocator with available memory.
     * Protection: Only use memory that exists (detected via CMOS)
     * Note: Low memory (0-1MB) is NOT used in this project
     * ------------------------------------------------------------------ */
    uint32_t max_phys_mapped = 0x40000000;  /* 1 GB pre-mapped region end */
    uint32_t max_phys_detected = mem_total_kb * 1024;  /* Actual RAM size */
    
    /* Use the minimum of mapped region and detected memory */
    uint32_t max_phys = (max_phys_detected < max_phys_mapped) 
                        ? max_phys_detected 
                        : max_phys_mapped;
    
    /* Calculate memory available for buddy allocator */
    /* Start from first free page after kernel (guaranteed >= 1MB) */
    uint32_t buddy_mem_kb = (max_phys - mem_first_free_phys) / 1024;
    
    printk("[MEM] Pre-mapped region: phys 0x00000000-0x3FFFFFFF (1 GB)\n");
    printk("[MEM] Detected RAM: %u KB (%u MB)\n", 
           mem_total_kb, mem_total_kb / 1024);
    printk("[MEM] Usable limit: phys 0x%08x (%u MB)\n",
           max_phys, max_phys / (1024 * 1024));
    printk("[MEM] Low memory (0-1MB): RESERVED, not used\n");
    printk("[MEM] Buddy allocator range: phys 0x%08x-0x%08x\n",
           mem_first_free_phys, max_phys);
    printk("[MEM] Buddy allocator memory: %u KB (%u MB)\n",
           buddy_mem_kb, buddy_mem_kb / 1024);
    
    buddy_init(mem_first_free_phys, buddy_mem_kb);
    
    /* Initialize slab allocator */
    slab_init();
    
    /* Initialize block device cache */
    cache_init();
    
    /* Initialize driver subsystem and register devices */
    driver_init();
    vga_register_driver();
    
    /* Initialize TTY after VGA */
    tty_init();
    tty_register_driver();
    
    pit_init(100);
    pit_register_driver();
    
    /* Initialize keyboard driver */
    kbd_init();
    kbd_register_driver();

    printk("Early initialization complete.\n\n");
    
    /* Initialize IDE driver */
    ide_init();
    ide_register_driver();
    ide_print_disks();
    
    /* Initialize MBR partition support */
    mbr_init();
    mbr_print_partitions();
    
    /* Initialize VFS */
    vfs_init();
    
    printk("\nKernel initialization complete.\n\n");
    char buf[512];

    for(int i=0;i<512;i++){
       buf[i] = i;
    }

    bwrite(5, 1, buf, 1, 1);
    cache_flush();
    
    /* Enable interrupts after all initialization is complete */
    sti();
    while(1);
}
 
