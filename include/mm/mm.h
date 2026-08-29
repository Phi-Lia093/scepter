#ifndef MM_H
#define MM_H

#include <stdint.h>
#include "arch/paging.h"

/* =========================================================================
 * Memory layout constants and phys<->virt helpers now live in
 * arch/paging.h (arch-specific).
 * ========================================================================= */

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
