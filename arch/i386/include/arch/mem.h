#ifndef ARCH_MEM_H
#define ARCH_MEM_H

#include <stdint.h>

/* =========================================================================
 * Physical memory detection – arch-neutral API.
 * ========================================================================= */

/**
 * arch_mem_detect_kb - Detect total physical memory installed.
 * @return Total RAM in kilobytes
 */
uint32_t arch_mem_detect_kb(void);

#endif /* ARCH_MEM_H */
