#ifndef PGTABLE_H
#define PGTABLE_H

#include <stdint.h>

/* ============================================================================
 * Page Table Management Utilities
 *
 * Provides low-level access to kernel page tables for dynamic memory mapping.
 * Used to manage the direct-mapped region and future vmalloc/ioremap support.
 * ============================================================================ */

/* Page size constant (defined in buddy.h but repeated for convenience) */
#define PAGE_SIZE  4096

/* ============================================================================
 * Page Table Entry Access
 * ============================================================================ */

/**
 * Get the Page Table Entry (PTE) for a virtual address
 * @param virt_addr Virtual address to look up
 * @return Pointer to PTE, or NULL if page table not present
 */
uint32_t* get_pte(uint32_t virt_addr);

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
int map_page(uint32_t *pgdir, uint32_t virt_addr, uint32_t phys_addr, uint32_t flags);

/**
 * Invalidate (unmap) a single virtual page
 * @param virt_addr Virtual address of page to unmap (will be page-aligned)
 */
void unmap_page(uint32_t virt_addr);

/**
 * Invalidate (unmap) a range of virtual pages
 * @param virt_start Start of virtual address range (inclusive)
 * @param virt_end End of virtual address range (exclusive)
 */
void unmap_range(uint32_t virt_start, uint32_t virt_end);

/**
 * Flush entire TLB (Translation Lookaside Buffer)
 * Forces reload of all page table entries from memory
 */
void flush_tlb(void);

/* ============================================================================
 * Page Table Statistics
 * ============================================================================ */

/**
 * Count mapped pages in a virtual address range
 * @param virt_start Start of range
 * @param virt_end End of range (exclusive)
 * @return Number of mapped pages
 */
uint32_t count_mapped_pages(uint32_t virt_start, uint32_t virt_end);

/* ============================================================================
 * Page Directory Management
 * ============================================================================ */

/**
 * Create a new user page directory with NO kernel mappings
 * @return Physical address of new page directory, or NULL on error
 */
uint32_t* create_user_pgdir(void);

#endif /* PGTABLE_H */
