#ifndef BUDDY_H
#define BUDDY_H

#include <stdint.h>
#include <stddef.h>

/* =========================================================================
 * Buddy Allocator - Physical Memory Manager
 *
 * Power-of-2 page allocator using the buddy system algorithm.
 * Manages physical memory starting from the end of the kernel image.
 * Returns virtual addresses (physical + KERNEL_VMA) for higher-half kernel.
 * ========================================================================= */

/* Page size: 4 KB */
#define PAGE_SIZE      4096
#define PAGE_SHIFT     12

/* Maximum order: 2^MAX_ORDER pages = 2^(MAX_ORDER + 12) bytes
 * MAX_ORDER = 10 → max allocation = 1024 pages = 4 MB */
#define MAX_ORDER      10

/* =========================================================================
 * Initialization
 * ========================================================================= */

/* Initialize the buddy allocator with available physical memory.
 * base_phys:  starting physical address (page-aligned)
 * total_kb:   total memory available in kilobytes
 * Must be called once during kernel initialization. */
void buddy_init(uint32_t base_phys, uint32_t total_kb);

/* =========================================================================
 * Allocation Flags
 * ========================================================================= */

/* MEM_PHY: Allocate physical memory anywhere, return physical address
 *          Use for DMA buffers, ACPI tables, or MMIO that will be remapped */
#define MEM_PHY  0x0001

/* MEM_MAP: Allocate from pre-mapped region, return virtual address
 *          Use for normal kernel allocations (default behavior) */
#define MEM_MAP  0x0002

/* =========================================================================
 * Allocation and Deallocation
 * ========================================================================= */

/* Allocate physical memory with specific flags.
 * size: number of bytes to allocate
 * flags: MEM_PHY (returns physical addr) or MEM_MAP (returns virtual addr)
 * Returns address or NULL on failure. */
void *page_alloc_flags(size_t size, uint32_t flags);

/* Allocate physical memory from pre-mapped region (convenience wrapper).
 * Returns a virtual address (physical + KERNEL_VMA) or NULL on failure.
 * Equivalent to page_alloc_flags(size, MEM_MAP). */
static inline void *page_alloc(size_t size) {
    return page_alloc_flags(size, MEM_MAP);
}

/* Free physical memory previously allocated with page_alloc().
 * addr: address returned by page_alloc() or page_alloc_flags()
 * Works with both physical and virtual addresses. */
void page_free(void *addr);

/* =========================================================================
 * Statistics
 * ========================================================================= */

/* Get total number of pages managed by the allocator */
uint32_t buddy_total_pages(void);

/* Get current number of free pages */
uint32_t buddy_free_pages(void);

/* Get current number of allocated pages */
uint32_t buddy_used_pages(void);

#endif /* BUDDY_H */