#ifndef INTERRUPT_H
#define INTERRUPT_H

#include <stdint.h>

/* Number of legacy IRQ lines we support (ISA IRQs 0-15). */
#define IRQ_COUNT 16

/* IRQ handler: receives the interrupted CS so it can tell user mode
 * (0x1B) from kernel mode (0x08) when it needs to. */
typedef void (*irq_handler_t)(uint32_t cs);

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

/* ============================================================================
 * Generic IRQ Dispatch
 *
 * All ISA IRQ stubs (irq0-irq15, kernel/isr.s) save the register frame and
 * call irq_dispatch(irq, cs).  Drivers register their handler with
 * irq_register() instead of installing their own IDT gate.
 * ============================================================================ */

/** IRQ stubs defined in kernel/isr.s (vector 0x20+irq in the IDT). */
extern void irq0(void);   extern void irq1(void);   extern void irq2(void);
extern void irq3(void);   extern void irq4(void);   extern void irq5(void);
extern void irq6(void);   extern void irq7(void);   extern void irq8(void);
extern void irq9(void);   extern void irq10(void);  extern void irq11(void);
extern void irq12(void);  extern void irq13(void);  extern void irq14(void);
extern void irq15(void);

/**
 * Install IDT gates for vectors 32-47 (IRQs 0-15).
 * Called once during kernel startup (after isr_init()).
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
 * Called from the IRQ stubs in kernel/isr.s.
 * @param irq IRQ number
 * @param cs  Interrupted code segment (0x08 kernel, 0x1B user)
 */
void irq_dispatch(uint32_t irq, uint32_t cs);

#endif /* INTERRUPT_H */