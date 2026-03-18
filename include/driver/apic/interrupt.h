#ifndef INTERRUPT_H
#define INTERRUPT_H

#include <stdint.h>

/* ============================================================================
 * Interrupt Controller Manager
 *
 * Abstracts APIC vs PIC interrupt controllers.
 * Automatically detects and initializes the best available controller.
 * ============================================================================ */

/* Interrupt controller modes */
typedef enum {
    INT_MODE_UNKNOWN = 0,
    INT_MODE_PIC,       /* Legacy 8259A PIC */
    INT_MODE_APIC       /* Modern Local APIC + I/O APIC */
} interrupt_mode_t;

/* ============================================================================
 * Function Prototypes
 * ============================================================================ */

/**
 * Initialize interrupt controller
 * Tries APIC first (if ACPI available), falls back to PIC
 */
void interrupt_init(void);

/**
 * Get current interrupt mode
 * @return Current interrupt controller mode
 */
interrupt_mode_t interrupt_get_mode(void);

/**
 * Send End-Of-Interrupt signal
 * @param irq IRQ number (for PIC mode)
 */
void interrupt_eoi(uint8_t irq);

/**
 * Enable specific IRQ
 * @param irq IRQ number (0-15 for PIC, 0-23+ for APIC)
 */
void interrupt_enable_irq(uint8_t irq);

/**
 * Disable specific IRQ
 * @param irq IRQ number
 */
void interrupt_disable_irq(uint8_t irq);

#endif /* INTERRUPT_H */