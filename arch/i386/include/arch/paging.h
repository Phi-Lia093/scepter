#ifndef ARCH_PAGING_H
#define ARCH_PAGING_H

#include <stdint.h>

/* =========================================================================
 * Paging / address-space API – arch-neutral interface.
 *
 * Defines the kernel address-space layout, the phys<->virt translation
 * helpers, and the page-table manipulation primitives.  The active arch
 * implements these (arch/i386/paging.c) and may use any page-table format
 * it likes underneath.
 * ========================================================================= */

/* Page size: 4 KB */
#define PAGE_SIZE  4096

/* Kernel address-space layout (Linux-style, higher-half) */
#define KERNEL_VMA              0xC0000000U  /* Kernel virtual base */
#define KERNEL_DIRECT_MAP_LIMIT 0x38000000U  /* 896 MB physical limit */
#define KERNEL_VMALLOC_BASE     0xF8000000U  /* vmalloc region start (reserved) */
#define KERNEL_VMALLOC_END      0xFFBFFFFFU  /* vmalloc region end (128 MB) */
#define KERNEL_FIXMAP_BASE      0xFFC00000U  /* Fixed mappings (4 MB, future) */

/* Helper macros for address conversion */
#define PHYS_TO_VIRT(phys)  ((void*)((uint32_t)(phys) + KERNEL_VMA))
#define VIRT_TO_PHYS(virt)  ((uint32_t)(virt) - KERNEL_VMA)

/* Boot page directory / tables (defined in arch/i386/boot.s) */
extern uint32_t boot_page_directory[];
extern uint32_t boot_page_tables[];

/**
 * arch_kernel_pgdir - Virtual address of the boot (kernel) page directory.
 */
uint32_t *arch_kernel_pgdir(void);

/**
 * arch_kernel_pgdir_phys - Physical address (CR3 value) of the boot page
 * directory.  Used as the kernel task's CR3 and to switch to kernel page
 * tables while tearing down a user address space.
 */
uint32_t arch_kernel_pgdir_phys(void);

/* =========================================================================
 * Page Table Entry Access
 * ========================================================================= */

/**
 * Get the Page Table Entry (PTE) for a virtual address.
 * @return Pointer to PTE, or NULL if page table not present
 */
uint32_t* get_pte(uint32_t virt_addr);

/* =========================================================================
 * Page Mapping Operations
 * ========================================================================= */

/**
 * Map a physical page to a virtual page in a specific page directory.
 * @param pgdir Page directory to map in (NULL = current/boot)
 * @return 0 on success, -1 on error
 */
int map_page(uint32_t *pgdir, uint32_t virt_addr, uint32_t phys_addr, uint32_t flags);

/**
 * Invalidate (unmap) a single virtual page.
 */
void unmap_page(uint32_t virt_addr);

/**
 * Invalidate (unmap) a range of virtual pages.
 */
void unmap_range(uint32_t virt_start, uint32_t virt_end);

/**
 * Flush the entire TLB.
 */
void flush_tlb(void);

/**
 * Count mapped pages in a virtual address range.
 */
uint32_t count_mapped_pages(uint32_t virt_start, uint32_t virt_end);

/* =========================================================================
 * Page Directory Management
 * ========================================================================= */

/**
 * Create a new user page directory with kernel mappings (supervisor-only).
 * @return Physical address of new page directory, or NULL on error
 */
uint32_t* create_user_pgdir(void);

#endif /* ARCH_PAGING_H */
