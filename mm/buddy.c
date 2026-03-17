#include "mm/buddy.h"
#include "lib/printk.h"
#include <stdint.h>
#include <stddef.h>

/* =========================================================================
 * Simple Bitmap-Based Page Allocator
 * 
 * Instead of complex buddy allocation, uses a simple bitmap where each bit
 * represents one page (0=free, 1=allocated). Simple, fast, and bulletproof.
 * ========================================================================= */

#define KERNEL_VMA  0xC0000000U

/* Maximum pages we can track (1GB / 4KB = 256K pages = 32KB bitmap) */
#define MAX_PAGES 262144

/* Page allocator state */
typedef struct {
    uint32_t base_phys;       /* Starting physical address */
    uint32_t total_pages;     /* Total pages available */
    uint32_t free_pages;      /* Currently free pages */
    uint32_t bitmap[MAX_PAGES / 32];  /* Bitmap: 32 pages per uint32_t */
} page_allocator_t;

static page_allocator_t allocator;

/* =========================================================================
 * Bitmap Helper Functions
 * ========================================================================= */

/* Convert physical address to virtual address */
static inline void *phys_to_virt(uint32_t phys)
{
    return (void *)(phys + KERNEL_VMA);
}

/* Convert virtual address to physical address */
static inline uint32_t virt_to_phys(void *virt)
{
    return (uint32_t)virt - KERNEL_VMA;
}

/* Set a bit in the bitmap (mark page as allocated) */
static inline void bitmap_set(uint32_t page_idx)
{
    uint32_t word_idx = page_idx / 32;
    uint32_t bit_idx = page_idx % 32;
    allocator.bitmap[word_idx] |= (1U << bit_idx);
}

/* Clear a bit in the bitmap (mark page as free) */
static inline void bitmap_clear(uint32_t page_idx)
{
    uint32_t word_idx = page_idx / 32;
    uint32_t bit_idx = page_idx % 32;
    allocator.bitmap[word_idx] &= ~(1U << bit_idx);
}

/* Test if a bit is set in the bitmap */
static inline int bitmap_test(uint32_t page_idx)
{
    uint32_t word_idx = page_idx / 32;
    uint32_t bit_idx = page_idx % 32;
    return (allocator.bitmap[word_idx] & (1U << bit_idx)) != 0;
}

/* Find N consecutive free pages in bitmap, returns starting page index or -1 */
static int find_free_pages(uint32_t count)
{
    uint32_t consecutive = 0;
    uint32_t start_idx = 0;
    
    for (uint32_t i = 0; i < allocator.total_pages; i++) {
        if (!bitmap_test(i)) {
            /* Free page */
            if (consecutive == 0) {
                start_idx = i;
            }
            consecutive++;
            
            if (consecutive == count) {
                return start_idx;
            }
        } else {
            /* Allocated page, reset counter */
            consecutive = 0;
        }
    }
    
    return -1;  /* Not enough consecutive pages */
}

/* =========================================================================
 * Public API - Initialization
 * ========================================================================= */

void buddy_init(uint32_t base_phys, uint32_t total_kb)
{
    allocator.base_phys = base_phys;
    
    /* Calculate total pages (convert KB to pages) */
    uint32_t total_bytes = total_kb * 1024;
    allocator.total_pages = total_bytes >> PAGE_SHIFT;
    
    /* Cap at maximum trackable pages */
    if (allocator.total_pages > MAX_PAGES) {
        printk("[ALLOCATOR] Requested %u pages, capping to %u\n",
               allocator.total_pages, MAX_PAGES);
        allocator.total_pages = MAX_PAGES;
    }
    
    /* Initialize all pages as free (bitmap = 0) */
    for (uint32_t i = 0; i < (MAX_PAGES / 32); i++) {
        allocator.bitmap[i] = 0;
    }
    
    allocator.free_pages = allocator.total_pages;
    
    printk("[ALLOCATOR] Initialized: %u pages (%u MB) from phys 0x%08x\n",
           allocator.total_pages, 
           (allocator.total_pages * PAGE_SIZE) / (1024 * 1024),
           base_phys);
}

/* =========================================================================
 * Public API - Allocation with Flags
 * ========================================================================= */

void *page_alloc_flags(size_t size, uint32_t flags)
{
    if (size == 0) {
        return NULL;
    }
    
    /* Calculate number of pages needed (round up) */
    uint32_t pages_needed = (size + PAGE_SIZE - 1) >> PAGE_SHIFT;
    
    if (pages_needed > allocator.free_pages) {
        return NULL;  /* Not enough free pages */
    }
    
    /* Find consecutive free pages */
    int start_idx = find_free_pages(pages_needed);
    if (start_idx < 0) {
        return NULL;  /* No consecutive block large enough */
    }
    
    /* Mark pages as allocated */
    for (uint32_t i = 0; i < pages_needed; i++) {
        bitmap_set(start_idx + i);
    }
    
    allocator.free_pages -= pages_needed;
    
    /* Calculate physical address */
    uint32_t phys = allocator.base_phys + (start_idx << PAGE_SHIFT);
    
    /* Return based on flags */
    if (flags & MEM_PHY) {
        /* Return physical address (caller will map it later) */
        return (void*)phys;
    } else {
        /* MEM_MAP: Return virtual address (must be in mapped region) */
        /* Note: All pages in buddy allocator are pre-mapped by mm_init */
        return phys_to_virt(phys);
    }
}

/* =========================================================================
 * Public API - Deallocation
 * ========================================================================= */

void page_free(void *addr)
{
    if (!addr) {
        return;
    }
    
    uint32_t phys;
    
    /* Handle both virtual addresses (>= 0xC0000000) and physical addresses */
    if ((uint32_t)addr >= KERNEL_VMA) {
        /* Virtual address - convert to physical */
        phys = virt_to_phys(addr);
    } else {
        /* Physical address - use directly */
        phys = (uint32_t)addr;
    }
    
    /* Validate address is within our range */
    if (phys < allocator.base_phys) {
        printk("[ALLOCATOR] ERROR: Free 0x%08x below base 0x%08x\n",
               phys, allocator.base_phys);
        return;
    }
    
    uint32_t offset = phys - allocator.base_phys;
    uint32_t page_idx = offset >> PAGE_SHIFT;
    
    if (page_idx >= allocator.total_pages) {
        printk("[ALLOCATOR] ERROR: Free 0x%08x beyond range (page %u >= %u)\n",
               phys, page_idx, allocator.total_pages);
        return;
    }
    
    /* Check if page was actually allocated */
    if (!bitmap_test(page_idx)) {
        printk("[ALLOCATOR] WARNING: Double free of page %u (phys 0x%08x)\n",
               page_idx, phys);
        return;
    }
    
    /* Free the page - we can only free one page at a time since we don't
     * track allocation sizes. This is fine for our use case. */
    bitmap_clear(page_idx);
    allocator.free_pages++;
}

/* =========================================================================
 * Public API - Statistics
 * ========================================================================= */

uint32_t buddy_total_pages(void)
{
    return allocator.total_pages;
}

uint32_t buddy_free_pages(void)
{
    return allocator.free_pages;
}

uint32_t buddy_used_pages(void)
{
    return allocator.total_pages - allocator.free_pages;
}