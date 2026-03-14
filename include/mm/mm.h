#ifndef MM_H
#define MM_H

#include <stdint.h>

/* Global memory information (set by mm_init) */
extern uint32_t mem_total_kb;
extern uint32_t mem_first_free_phys;

/**
 * Initialize memory management subsystem
 * - Detects total RAM via CMOS
 * - Calculates free memory range
 * - Initializes buddy allocator
 * - Initializes slab allocator
 */
void mm_init(void);

#endif /* MM_H */