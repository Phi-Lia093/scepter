#ifndef MM_H
#define MM_H

#include <stdint.h>

/* Memory layout constants (Linux-style) */
#define KERNEL_VMA              0xC0000000U  /* Kernel virtual base */
#define KERNEL_DIRECT_MAP_LIMIT 0x38000000U  /* 896 MB physical limit */
#define KERNEL_VMALLOC_BASE     0xF8000000U  /* vmalloc region start (reserved) */
#define KERNEL_VMALLOC_END      0xFFBFFFFFU  /* vmalloc region end (128 MB) */
#define KERNEL_FIXMAP_BASE      0xFFC00000U  /* Fixed mappings (4 MB, future) */

/* Helper macros for address conversion */
#define PHYS_TO_VIRT(phys)  ((void*)((uint32_t)(phys) + KERNEL_VMA))
#define VIRT_TO_PHYS(virt)  ((uint32_t)(virt) - KERNEL_VMA)

/* Global memory information (set by mm_init) */
extern uint32_t mem_total_kb;          /* Total detected RAM in KB */
extern uint32_t mem_first_free_phys;   /* First free physical page after kernel */
extern uint32_t mem_direct_map_size;   /* Size of direct-mapped region (1/4 RAM) */

/**
 * Initialize memory management subsystem
 * - Detects total RAM via CMOS
 * - Calculates direct-mapped region size (1/4 RAM, min 32MB, max 896MB)
 * - Initializes buddy allocator for direct-mapped region
 * - Initializes slab allocator
 * - Reserves upper 128 MB for future vmalloc/ioremap
 */
void mm_init(void);

#endif /* MM_H */
