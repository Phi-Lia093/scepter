/* ============================================================================
 * Virtual Memory Allocator - ioremap/iounmap Implementation
 * ============================================================================ */

#include "mm/vmalloc.h"
#include "mm/mm.h"
#include "mm/buddy.h"
#include "mm/pgtable.h"
#include "lib/printk.h"
#include "lib/string.h"
#include <stddef.h>

/* ============================================================================
 * Bitmap Management (Track allocated virtual pages)
 * ============================================================================ */

/* Number of pages in vmalloc region: 128 MB / 4 KB = 32768 pages */
#define VMALLOC_PAGES  (VMALLOC_SIZE / PAGE_SIZE)

/* Bitmap: 32768 pages / 32 bits = 1024 uint32_t = 4 KB */
static uint32_t vmalloc_bitmap[VMALLOC_PAGES / 32];

/* Statistics */
static uint32_t vmalloc_used_page_count = 0;

/* ============================================================================
 * Region Tracking (Track active mappings)
 * ============================================================================ */

typedef struct {
    uint32_t virt_addr;    /* Virtual address (page-aligned) */
    uint32_t phys_addr;    /* Physical address (page-aligned) */
    uint32_t num_pages;    /* Number of pages mapped */
    uint32_t in_use;       /* 1 if allocated, 0 if free */
} vmalloc_region_t;

#define MAX_VMALLOC_REGIONS 64
static vmalloc_region_t vmalloc_regions[MAX_VMALLOC_REGIONS];

/* ============================================================================
 * Bitmap Helper Functions
 * ============================================================================ */

static inline void bitmap_set_bit(uint32_t bit)
{
    vmalloc_bitmap[bit / 32] |= (1U << (bit % 32));
}

static inline void bitmap_clear_bit(uint32_t bit)
{
    vmalloc_bitmap[bit / 32] &= ~(1U << (bit % 32));
}

static inline int bitmap_test_bit(uint32_t bit)
{
    return (vmalloc_bitmap[bit / 32] & (1U << (bit % 32))) != 0;
}

/* ============================================================================
 * Virtual Address Allocation
 * ============================================================================ */

/**
 * Find N consecutive free pages in vmalloc region
 * @param num_pages Number of consecutive pages needed
 * @return Virtual address of first page, or 0 if not found
 */
static uint32_t vmalloc_find_free(uint32_t num_pages)
{
    uint32_t consecutive = 0;
    uint32_t start_page = 0;
    
    for (uint32_t page = 0; page < VMALLOC_PAGES; page++) {
        if (!bitmap_test_bit(page)) {
            /* Free page */
            if (consecutive == 0) {
                start_page = page;
            }
            consecutive++;
            
            if (consecutive == num_pages) {
                /* Found enough consecutive pages */
                return VMALLOC_START + (start_page * PAGE_SIZE);
            }
        } else {
            /* Used page, reset counter */
            consecutive = 0;
        }
    }
    
    return 0;  /* Not found */
}

/**
 * Mark pages as allocated in bitmap
 */
static void vmalloc_mark_used(uint32_t virt_addr, uint32_t num_pages)
{
    uint32_t start_page = (virt_addr - VMALLOC_START) / PAGE_SIZE;
    
    for (uint32_t i = 0; i < num_pages; i++) {
        bitmap_set_bit(start_page + i);
    }
    
    vmalloc_used_page_count += num_pages;
}

/**
 * Mark pages as free in bitmap
 */
static void vmalloc_mark_free(uint32_t virt_addr, uint32_t num_pages)
{
    uint32_t start_page = (virt_addr - VMALLOC_START) / PAGE_SIZE;
    
    for (uint32_t i = 0; i < num_pages; i++) {
        bitmap_clear_bit(start_page + i);
    }
    
    vmalloc_used_page_count -= num_pages;
}

/* ============================================================================
 * Region Tracking
 * ============================================================================ */

static vmalloc_region_t* vmalloc_add_region(uint32_t virt_addr, uint32_t phys_addr, 
                                             uint32_t num_pages)
{
    for (int i = 0; i < MAX_VMALLOC_REGIONS; i++) {
        if (!vmalloc_regions[i].in_use) {
            vmalloc_regions[i].virt_addr = virt_addr;
            vmalloc_regions[i].phys_addr = phys_addr;
            vmalloc_regions[i].num_pages = num_pages;
            vmalloc_regions[i].in_use = 1;
            return &vmalloc_regions[i];
        }
    }
    return NULL;
}

static vmalloc_region_t* vmalloc_find_region(uint32_t virt_addr)
{
    uint32_t virt_base = virt_addr & ~0xFFF;
    
    for (int i = 0; i < MAX_VMALLOC_REGIONS; i++) {
        if (vmalloc_regions[i].in_use && 
            vmalloc_regions[i].virt_addr == virt_base) {
            return &vmalloc_regions[i];
        }
    }
    return NULL;
}

static void vmalloc_remove_region(vmalloc_region_t *region)
{
    region->in_use = 0;
}

/* ============================================================================
 * Initialization
 * ============================================================================ */

void vmalloc_init(void)
{
    printk("[VMALLOC] Initializing virtual memory allocator...\n");
    
    /* Clear bitmap */
    memset(vmalloc_bitmap, 0, sizeof(vmalloc_bitmap));
    
    /* Clear region tracking */
    memset(vmalloc_regions, 0, sizeof(vmalloc_regions));
    
    vmalloc_used_page_count = 0;
    
    printk("[VMALLOC] Region: 0x%08x-0x%08x (%u MB, %u pages)\n",
           VMALLOC_START, VMALLOC_END, 
           VMALLOC_SIZE / (1024 * 1024),
           VMALLOC_PAGES);
    printk("[VMALLOC] Initialized successfully\n");
}

/* ============================================================================
 * ioremap/iounmap Implementation
 * ============================================================================ */

void *ioremap(uint32_t phys_addr, uint32_t size)
{
    if (size == 0) {
        return NULL;
    }
    
    /* Page-align address and size */
    uint32_t phys_base = phys_addr & ~0xFFF;
    uint32_t offset = phys_addr & 0xFFF;
    uint32_t num_pages = (size + offset + PAGE_SIZE - 1) / PAGE_SIZE;
    
    /* Find free virtual address range */
    uint32_t virt_base = vmalloc_find_free(num_pages);
    if (virt_base == 0) {
        printk("[IOREMAP] ERROR: No virtual space for %u pages\n", num_pages);
        return NULL;
    }
    
    /* Map each page (NULL = use boot page directory for kernel mappings) */
    for (uint32_t i = 0; i < num_pages; i++) {
        uint32_t virt = virt_base + (i * PAGE_SIZE);
        uint32_t phys = phys_base + (i * PAGE_SIZE);
        
        if (map_page(NULL, virt, phys, 0x3) < 0) {  /* Present | Writable */
            printk("[IOREMAP] ERROR: Failed to map page %u\n", i);
            /* Unmap what we've mapped so far */
            for (uint32_t j = 0; j < i; j++) {
                unmap_page(virt_base + (j * PAGE_SIZE));
            }
            return NULL;
        }
    }
    
    /* Mark as used in bitmap */
    vmalloc_mark_used(virt_base, num_pages);
    
    /* Record the mapping */
    if (!vmalloc_add_region(virt_base, phys_base, num_pages)) {
        printk("[IOREMAP] WARNING: Region tracking full\n");
    }
    
    printk("[IOREMAP] Mapped phys 0x%08x (%u KB) → virt 0x%08x\n",
           phys_addr, (num_pages * PAGE_SIZE) / 1024, virt_base + offset);
    
    return (void *)(virt_base + offset);
}

void iounmap(void *virt_addr)
{
    if (!virt_addr) {
        return;
    }
    
    uint32_t virt_base = (uint32_t)virt_addr & ~0xFFF;
    
    /* Find the region */
    vmalloc_region_t *region = vmalloc_find_region(virt_base);
    if (!region) {
        printk("[IOUNMAP] WARNING: Address 0x%08x not found\n", virt_base);
        return;
    }
    
    /* Unmap all pages */
    for (uint32_t i = 0; i < region->num_pages; i++) {
        unmap_page(virt_base + (i * PAGE_SIZE));
    }
    
    /* Mark as free in bitmap */
    vmalloc_mark_free(virt_base, region->num_pages);
    
    printk("[IOUNMAP] Unmapped virt 0x%08x (%u pages)\n",
           virt_base, region->num_pages);
    
    /* Remove from tracking */
    vmalloc_remove_region(region);
}

/* ============================================================================
 * Statistics
 * ============================================================================ */

uint32_t vmalloc_used_pages(void)
{
    return vmalloc_used_page_count;
}

uint32_t vmalloc_free_pages(void)
{
    return VMALLOC_PAGES - vmalloc_used_page_count;
}

void vmalloc_print_stats(void)
{
    printk("[VMALLOC] Statistics:\n");
    printk("[VMALLOC]   Total pages: %u (%u MB)\n",
           VMALLOC_PAGES, VMALLOC_SIZE / (1024 * 1024));
    printk("[VMALLOC]   Used pages:  %u (%u KB)\n",
           vmalloc_used_page_count, 
           (vmalloc_used_page_count * PAGE_SIZE) / 1024);
    printk("[VMALLOC]   Free pages:  %u (%u KB)\n",
           vmalloc_free_pages(),
           (vmalloc_free_pages() * PAGE_SIZE) / 1024);
}