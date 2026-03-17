/* ============================================================================
 * Page Table Management Utilities
 * ============================================================================ */

#include "mm/mm.h"
#include "mm/buddy.h"
#include "lib/printk.h"
#include <stdint.h>
#include <stddef.h>

/* External: Boot page directory and tables from boot.s */
extern uint32_t boot_page_directory[];
extern uint32_t boot_page_tables[];

/* ============================================================================
 * Page Table Entry Access
 * ============================================================================ */

/**
 * Get the Page Table Entry (PTE) for a virtual address
 * @param virt_addr Virtual address to look up
 * @return Pointer to PTE, or NULL if page table not present
 */
uint32_t* get_pte(uint32_t virt_addr)
{
    uint32_t pde_idx = virt_addr >> 22;           /* Top 10 bits */
    uint32_t pte_idx = (virt_addr >> 12) & 0x3FF; /* Middle 10 bits */
    
    /* Get page directory entry */
    uint32_t pde = boot_page_directory[pde_idx];
    if (!(pde & 0x1)) {
        return NULL;  /* Page directory entry not present */
    }
    
    /* Get page table (convert physical address to virtual) */
    uint32_t* pt = (uint32_t*)((pde & ~0xFFF) + KERNEL_VMA);
    return &pt[pte_idx];
}

/* ============================================================================
 * Page Mapping Operations
 * ============================================================================ */

/**
 * Invalidate (unmap) a single virtual page
 * @param virt_addr Virtual address of page to unmap (will be page-aligned)
 */
void unmap_page(uint32_t virt_addr)
{
    virt_addr &= ~0xFFF;  /* Page-align address */
    
    uint32_t* pte = get_pte(virt_addr);
    if (pte && (*pte & 0x1)) {
        *pte = 0;  /* Mark as not present */
        
        /* Flush TLB entry for this page */
        asm volatile("invlpg (%0)" :: "r"(virt_addr) : "memory");
    }
}

/**
 * Invalidate (unmap) a range of virtual pages
 * @param virt_start Start of virtual address range (inclusive)
 * @param virt_end End of virtual address range (exclusive)
 */
void unmap_range(uint32_t virt_start, uint32_t virt_end)
{
    /* Page-align addresses */
    virt_start &= ~0xFFF;
    virt_end = (virt_end + 0xFFF) & ~0xFFF;
    
    /* Handle wrap-around case (when virt_end is near 0xFFFFFFFF) */
    if (virt_end == 0 || virt_end <= virt_start) {
        /* Don't try to unmap if range is invalid */
        return;
    }
    
    uint32_t pages_unmapped = 0;
    
    for (uint32_t addr = virt_start; addr < virt_end && addr >= virt_start; addr += PAGE_SIZE) {
        unmap_page(addr);
        pages_unmapped++;
    }
    
    printk("[PGTABLE] Unmapped %u pages (0x%08x-0x%08x)\n",
           pages_unmapped, virt_start, virt_end - 1);
}

/**
 * Flush entire TLB (Translation Lookaside Buffer)
 */
void flush_tlb(void)
{
    uint32_t cr3;
    asm volatile("mov %%cr3, %0" : "=r"(cr3));
    asm volatile("mov %0, %%cr3" :: "r"(cr3) : "memory");
}

/* ============================================================================
 * Page Table Statistics
 * ============================================================================ */

/**
 * Count mapped pages in a virtual address range
 * @param virt_start Start of range
 * @param virt_end End of range (exclusive)
 * @return Number of mapped pages
 */
uint32_t count_mapped_pages(uint32_t virt_start, uint32_t virt_end)
{
    virt_start &= ~0xFFF;
    virt_end = (virt_end + 0xFFF) & ~0xFFF;
    
    uint32_t mapped = 0;
    
    for (uint32_t addr = virt_start; addr < virt_end; addr += PAGE_SIZE) {
        uint32_t* pte = get_pte(addr);
        if (pte && (*pte & 0x1)) {
            mapped++;
        }
    }
    
    return mapped;
}