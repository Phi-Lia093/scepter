/* ============================================================================
 * Memory Management Initialization
 * ============================================================================ */

#include "mm/buddy.h"
#include "mm/slab.h"
#include "lib/printk.h"
#include "kernel/asm.h"

/* External: Linker-provided symbol for end of kernel image */
extern char kernel_end[];
#define KERNEL_VMA  0xC0000000U

/* Global memory information */
uint32_t mem_total_kb = 0;
uint32_t mem_first_free_phys = 0;

/* ============================================================================
 * CMOS Memory Detection
 * ============================================================================ */

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

/* ============================================================================
 * Memory Management Initialization
 * ============================================================================ */

void mm_init(void)
{
    printk("\n[MM] Initializing memory management...\n");
    
    /* Detect total memory via CMOS */
    mem_total_kb = detect_memory_cmos();
    printk("[MM] Detected %u KB (%u MB) via CMOS\n",
           mem_total_kb, mem_total_kb / 1024);

    /* Calculate first free physical page after kernel */
    uint32_t kernel_end_phys = (uint32_t)kernel_end - KERNEL_VMA;
    mem_first_free_phys = (kernel_end_phys + 0xFFF) & ~0xFFFU;

    printk("[MM] Kernel image end: phys=0x%08x  virt=0x%08x\n",
           kernel_end_phys, (uint32_t)kernel_end);
    printk("[MM] First free page:  phys=0x%08x\n",
           mem_first_free_phys);

    /* Calculate usable memory range */
    uint32_t max_phys_mapped   = 0x40000000U;  /* 1 GB pre-mapped */
    uint32_t max_phys_detected = mem_total_kb * 1024;
    uint32_t max_phys = (max_phys_detected < max_phys_mapped)
                        ? max_phys_detected : max_phys_mapped;

    uint32_t buddy_mem_kb = (max_phys - mem_first_free_phys) / 1024;

    printk("[MM] Pre-mapped region: phys 0x00000000-0x3FFFFFFF (1 GB)\n");
    printk("[MM] Detected RAM:      %u KB (%u MB)\n",
           mem_total_kb, mem_total_kb / 1024);
    printk("[MM] Usable limit:      phys 0x%08x (%u MB)\n",
           max_phys, max_phys / (1024 * 1024));
    printk("[MM] Low memory 0-1MB:  RESERVED\n");
    printk("[MM] Buddy range:       phys 0x%08x-0x%08x\n",
           mem_first_free_phys, max_phys);
    printk("[MM] Buddy memory:      %u KB (%u MB)\n\n",
           buddy_mem_kb, buddy_mem_kb / 1024);

    /* Initialize buddy allocator */
    buddy_init(mem_first_free_phys, buddy_mem_kb);
    
    /* Initialize slab allocator */
    slab_init();
    
    printk("[MM] Memory management initialized\n\n");
}