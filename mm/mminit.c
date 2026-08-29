/* ============================================================================
 * Memory Management Initialization
 * ============================================================================ */

#include "mm/buddy.h"
#include "mm/slab.h"
#include "mm/mm.h"
#include "mm/vmalloc.h"
#include "arch/mem.h"
#include "lib/printk.h"

/* External: Linker-provided symbol for end of kernel image */
extern char kernel_end[];

/* Global memory information */
uint32_t mem_total_kb = 0;
uintptr_t mem_first_free_phys = 0;
uintptr_t mem_direct_map_size = 0;  /* Actual size of direct-mapped region */

/* ============================================================================
 * Memory Management Initialization
 * ============================================================================ */

void mm_init(void)
{
    printk("\n[MM] Initializing memory management...\n");
    
    /* Detect total memory via the arch (CMOS on i386) */
    mem_total_kb = arch_mem_detect_kb();
    uint32_t total_phys = mem_total_kb * 1024;
    printk("[MM] Detected %u KB (%u MB) via CMOS\n",
           mem_total_kb, mem_total_kb / 1024);

    /* Calculate first free physical page after kernel */
    uintptr_t kernel_end_phys = (uintptr_t)kernel_end - KERNEL_VMA;
    mem_first_free_phys = (kernel_end_phys + 0xFFF) & ~0xFFFU;

    printk("[MM] Kernel image end: phys=0x%08lx  virt=0x%08lx\n",
           (unsigned long)kernel_end_phys, (unsigned long)(uintptr_t)kernel_end);
    printk("[MM] First free page:  phys=0x%08lx\n",
           (unsigned long)mem_first_free_phys);
    
    /* CRITICAL: Verify we're not allocating from low memory (below 1MB) */
    if (mem_first_free_phys < 0x100000) {
        printk("[MM] ERROR: Kernel ends below 1MB (0x%08lx)!\n", (unsigned long)mem_first_free_phys);
        printk("[MM] This would cause buddy allocator to use low memory.\n");
        printk("[MM] Low memory (0x0-0x100000) is reserved for BIOS/DMA.\n");
        while(1);  /* Halt - cannot continue safely */
    }
    printk("[MM] ✓ Low memory check passed (kernel above 1MB)\n");

    /* Calculate direct-mapped region size (Linux-style):
     * - Map 1/4 of total physical RAM
     * - Minimum: 32 MB
     * - Maximum: 896 MB (to reserve upper 128 MB for vmalloc/ioremap)
     */
    mem_direct_map_size = total_phys / 4;
    
    /* Enforce minimum of 32 MB */
    if (mem_direct_map_size < 32 * 1024 * 1024) {
        mem_direct_map_size = 32 * 1024 * 1024;
    }
    
    /* Enforce maximum of 896 MB */
    if (mem_direct_map_size > KERNEL_DIRECT_MAP_LIMIT) {
        mem_direct_map_size = KERNEL_DIRECT_MAP_LIMIT;
    }
    
    /* Also cap by detected RAM */
    if (mem_direct_map_size > total_phys) {
        mem_direct_map_size = total_phys;
    }

    uint32_t buddy_mem_kb = (mem_direct_map_size - mem_first_free_phys) / 1024;
    uintptr_t direct_map_end_virt = KERNEL_VMA + mem_direct_map_size;

    printk("\n[MM] Memory Layout (Linux-style):\n");
    printk("[MM]   Direct-mapped:  0xC0000000-0x%08lx (%u MB)\n",
           (unsigned long)(direct_map_end_virt - 1), mem_direct_map_size / (1024 * 1024));
    printk("[MM]   ├─ maps phys:   0x00000000-0x%08lx\n",
           (unsigned long)(mem_direct_map_size - 1));
    printk("[MM]   ├─ kernel code: 0xC0000000-0x%08lx\n",
           (unsigned long)((uintptr_t)kernel_end - 1));
    printk("[MM]   └─ buddy pages: 0x%08lx-0x%08lx\n",
           (unsigned long)(KERNEL_VMA + mem_first_free_phys), (unsigned long)(direct_map_end_virt - 1));
    printk("[MM]   Reserved high:  0xF8000000-0xFFBFFFFF (128 MB, vmalloc/ioremap)\n");
    printk("[MM]   Fixed mappings: 0xFFC00000-0xFFFFFFFF (4 MB, future)\n");
    
    printk("\n[MM] Buddy allocator: %u KB (%u MB) from phys 0x%08lx\n",
           buddy_mem_kb, buddy_mem_kb / 1024, (unsigned long)mem_first_free_phys);

    /* Initialize buddy allocator with direct-mapped region only */
    buddy_init(mem_first_free_phys, buddy_mem_kb);
    
    /* CRITICAL: Invalidate page table entries beyond direct-map region
     * boot.s mapped full 1GB (0x0-0x3FFFFFFF → 0xC0000000-0xFFFFFFFF)
     * but we only want to use what we calculated above.
     * This prevents accidental access to unmapped memory and prepares
     * the upper region (0xF8000000+) for future vmalloc/ioremap */
    
    printk("\n[MM] Updating page tables:\n");
    printk("[MM]   Keeping mapped:   0xC0000000-0x%08lx (%u MB)\n",
           (unsigned long)(direct_map_end_virt - 1), mem_direct_map_size / (1024 * 1024));
    
    /* Unmap everything from end of direct-map to end of address space
     * This includes both the gap and the vmalloc region */
    if (direct_map_end_virt < 0xFFFFFFFF) {
        printk("[MM]   Invalidating:     0x%08lx-0xFFFFFFFF\n", (unsigned long)direct_map_end_virt);
        
        /* Unmap in chunks to avoid overflow issues */
        uintptr_t chunk_start = direct_map_end_virt;
        uintptr_t chunk_end;
        
        while (chunk_start < 0xFFFFF000) {  /* Stop before last page */
            chunk_end = chunk_start + 0x10000000;  /* 256 MB chunks */
            if (chunk_end > 0xFFFFF000 || chunk_end < chunk_start) {
                chunk_end = 0xFFFFF000;  /* Last chunk before final page */
            }
            unmap_range(chunk_start, chunk_end);
            chunk_start = chunk_end;
        }
        
        /* Handle the last page specially (0xFFFFF000-0xFFFFFFFF) */
        unmap_page(0xFFFFF000);
        printk("[MM]   Unmapped last page: 0xFFFFF000\n");
    }
    
    /* Verify page table state */
    uint32_t mapped = count_mapped_pages(KERNEL_VMA, KERNEL_VMA + 0x40000000);
    printk("[MM]   Verified mapped:  %u pages (%u MB)\n",
           mapped, (mapped * PAGE_SIZE) / (1024 * 1024));
    
    /* Initialize slab allocator */
    slab_init();
    
    /* Initialize vmalloc/ioremap subsystem */
    vmalloc_init();
    
    printk("[MM] Memory management initialized\n\n");
}
