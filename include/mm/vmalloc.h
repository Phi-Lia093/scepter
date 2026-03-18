#ifndef VMALLOC_H
#define VMALLOC_H

#include <stdint.h>

/* ============================================================================
 * Virtual Memory Allocator (vmalloc/ioremap)
 *
 * Manages the high memory region (0xF8000000-0xFFBFFFFF = 128 MB)
 * for dynamic mapping of physical addresses.
 *
 * Use cases:
 *   - ACPI tables in high physical memory
 *   - Device MMIO regions
 *   - PCI configuration space
 * ============================================================================ */

/* vmalloc region constants */
#define VMALLOC_START  0xF8000000U
#define VMALLOC_END    0xFFBFFFFFU
#define VMALLOC_SIZE   (VMALLOC_END - VMALLOC_START + 1)

/* Page flags for ioremap */
#define IOREMAP_NOCACHE    0x0010  /* Disable cache (for MMIO) */
#define IOREMAP_WRITECOMBINE 0x0020  /* Write combining */

/* ============================================================================
 * Initialization
 * ============================================================================ */

/**
 * Initialize vmalloc subsystem
 * Must be called during kernel initialization
 */
void vmalloc_init(void);

/* ============================================================================
 * Dynamic Physical Memory Mapping (ioremap/iounmap)
 * ============================================================================ */

/**
 * Map physical memory into virtual address space
 * 
 * @param phys_addr Physical address to map (can be unaligned)
 * @param size Size in bytes to map
 * @return Virtual address, or NULL on failure
 * 
 * Example:
 *   void *acpi_table = ioremap(0x07FE0000, 4096);
 *   // Access the table at acpi_table
 *   iounmap(acpi_table);
 */
void *ioremap(uint32_t phys_addr, uint32_t size);

/**
 * Unmap physical memory from virtual address space
 * 
 * @param virt_addr Virtual address returned by ioremap()
 * 
 * Must be called to free virtual address space when done.
 */
void iounmap(void *virt_addr);

/* ============================================================================
 * Statistics
 * ============================================================================ */

/**
 * Get number of used pages in vmalloc region
 * @return Number of allocated pages
 */
uint32_t vmalloc_used_pages(void);

/**
 * Get number of free pages in vmalloc region
 * @return Number of free pages
 */
uint32_t vmalloc_free_pages(void);

/**
 * Print vmalloc statistics
 */
void vmalloc_print_stats(void);

#endif /* VMALLOC_H */