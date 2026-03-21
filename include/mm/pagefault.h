#ifndef PAGEFAULT_H
#define PAGEFAULT_H

#include <stdint.h>

/* =========================================================================
 * Page Fault Handler
 * ========================================================================= */

/* Page fault error code bits */
#define PF_PRESENT   0x01  /* 0 = not present, 1 = protection violation */
#define PF_WRITE     0x02  /* 0 = read, 1 = write */
#define PF_USER      0x04  /* 0 = kernel, 1 = user */
#define PF_RESERVED  0x08  /* 1 = reserved bit set in page table */
#define PF_INSTR     0x10  /* 1 = instruction fetch */

/**
 * Page fault handler - called from ISR 14
 * @param error_code Page fault error code
 * @param fault_addr Faulting address (from CR2)
 */
void page_fault_handler(uint32_t error_code, uint32_t fault_addr);

/**
 * Initialize page fault handler
 */
void pagefault_init(void);

#endif /* PAGEFAULT_H */