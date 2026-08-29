#ifndef ARCH_IRQ_H
#define ARCH_IRQ_H

#include <stdint.h>

/* =========================================================================
 * Interrupt controller + IRQ dispatch – arch-neutral API.
 * Generic kernel code includes <arch/irq.h>.
 * ========================================================================= */

/* Number of legacy IRQ lines we support (ISA IRQs 0-15). */
#define IRQ_COUNT 16

/* IRQ handler: receives the interrupted CS so it can tell user mode
 * (0x1B) from kernel mode (0x08) when it needs to. */
typedef void (*irq_handler_t)(uint32_t cs);

/* Interrupt controller modes */
typedef enum {
    INT_MODE_UNKNOWN = 0,
    INT_MODE_PIC,       /* Legacy 8259A PIC */
    INT_MODE_APIC       /* Modern Local APIC + I/O APIC */
} interrupt_mode_t;

/**
 * Initialize the interrupt controller.
 * Tries APIC first (if ACPI available), falls back to PIC.
 */
void interrupt_init(void);

/**
 * Get current interrupt controller mode.
 */
interrupt_mode_t interrupt_get_mode(void);

/**
 * Send End-Of-Interrupt signal.
 * @param irq IRQ number (for PIC mode)
 */
void interrupt_eoi(uint8_t irq);

/**
 * Enable a specific IRQ on the active controller.
 */
void interrupt_enable_irq(uint8_t irq);

/**
 * Disable a specific IRQ on the active controller.
 */
void interrupt_disable_irq(uint8_t irq);

/**
 * Install IDT gates for vectors 32-47 (IRQs 0-15).
 * Called once during kernel startup (after the exception gates).
 */
void irq_init(void);

/**
 * Register an IRQ handler and enable the IRQ on the active controller.
 * @param irq     IRQ number (0-15)
 * @param handler Handler to call from irq_dispatch()
 */
void irq_register(int irq, irq_handler_t handler);

/**
 * Dispatch an IRQ to its registered handler and then send EOI.
 * Called from the arch IRQ stubs.
 * @param irq IRQ number
 * @param cs  Interrupted code segment (0x08 kernel, 0x1B user)
 */
void irq_dispatch(uint32_t irq, uint32_t cs);

#endif /* ARCH_IRQ_H */
