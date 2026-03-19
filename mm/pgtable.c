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

/* Forward declaration for allocation */
extern void *page_alloc_flags(size_t size, uint32_t flags);

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
 * Map a physical page to a virtual page in a specific page directory
 * @param pgdir Page directory to map in (NULL = current/boot)
 * @param virt_addr Virtual address (will be page-aligned)
 * @param phys_addr Physical address (will be page-aligned)
 * @param flags Page flags (Present | Writable | User | etc.)
 * @return 0 on success, -1 on error
 */
int map_page(uint32_t *pgdir, uint32_t virt_addr, uint32_t phys_addr, uint32_t flags)
{
    /* Use boot page directory if none specified */
    if (!pgdir) {
        pgdir = boot_page_directory;
    }
    
    /* Page-align addresses */
    virt_addr &= ~0xFFF;
    phys_addr &= ~0xFFF;
    
    /* Get page directory entry */
    uint32_t pde_idx = virt_addr >> 22;
    uint32_t *pde = &pgdir[pde_idx];
    
    /* Allocate page table if not present */
    if (!(*pde & 0x1)) {
        /* Allocate physical page for page table */
        void *pt_phys = page_alloc_flags(PAGE_SIZE, MEM_PHY);
        if (!pt_phys) {
            printk("[PGTABLE] ERROR: Cannot allocate page table\n");
            return -1;
        }
        
        /* Set page directory entry */
        *pde = (uint32_t)pt_phys | 0x3;  /* Present | Writable */
        
        /* Zero the page table (access via direct-mapped region)
         * Page tables themselves are allocated from direct-mapped region */
        uint32_t *pt_virt = (uint32_t *)PHYS_TO_VIRT((uint32_t)pt_phys);
        for (int i = 0; i < 1024; i++) {
            pt_virt[i] = 0;
        }
    }
    
    /* Get page table and set PTE */
    uint32_t *pt = (uint32_t *)((*pde & ~0xFFF) + KERNEL_VMA);
    uint32_t pte_idx = (virt_addr >> 12) & 0x3FF;
    
    pt[pte_idx] = phys_addr | flags;
    
    /* Flush TLB for this address */
    asm volatile("invlpg (%0)" :: "r"(virt_addr) : "memory");
    
    return 0;
}

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