#ifndef IOAPIC_H
#define IOAPIC_H

#include <stdint.h>
#include <stdbool.h>

/* ============================================================================
 * I/O APIC (I/O Advanced Programmable Interrupt Controller)
 *
 * Routes external interrupts (from devices) to Local APICs.
 * Memory-mapped, address provided by ACPI MADT.
 * Supports 24+ interrupt inputs (vs PIC's 16).
 * ============================================================================ */

/* I/O APIC Register Selectors */
#define IOAPIC_REG_ID       0x00    /* I/O APIC ID */
#define IOAPIC_REG_VER      0x01    /* I/O APIC Version */
#define IOAPIC_REG_ARB      0x02    /* I/O APIC Arbitration ID */
#define IOAPIC_REDTBL_BASE  0x10    /* Redirection Table Base */

/* I/O APIC Redirection Table Entry bits */
#define IOAPIC_DELMODE_FIXED    0x00000   /* Fixed delivery */
#define IOAPIC_DELMODE_LOWEST   0x00100   /* Lowest priority */
#define IOAPIC_DELMODE_SMI      0x00200   /* SMI */
#define IOAPIC_DELMODE_NMI      0x00400   /* NMI */
#define IOAPIC_DELMODE_INIT     0x00500   /* INIT */
#define IOAPIC_DELMODE_EXTINT   0x00700   /* ExtINT */

#define IOAPIC_DESTMODE_PHYSICAL 0x00000  /* Physical destination */
#define IOAPIC_DESTMODE_LOGICAL  0x00800  /* Logical destination */

#define IOAPIC_DELIVS_IDLE      0x00000   /* Delivery status idle */
#define IOAPIC_DELIVS_PENDING   0x01000   /* Delivery status pending */

#define IOAPIC_INTPOL_HIGH      0x00000   /* Active high */
#define IOAPIC_INTPOL_LOW       0x02000   /* Active low */

#define IOAPIC_TRIGGER_EDGE     0x00000   /* Edge triggered */
#define IOAPIC_TRIGGER_LEVEL    0x08000   /* Level triggered */

#define IOAPIC_MASKED           0x10000   /* Interrupt masked */

/* ============================================================================
 * Function Prototypes
 * ============================================================================ */

/**
 * Initialize I/O APIC
 * @param phys_addr Physical address of I/O APIC (from MADT)
 * @param gsi_base Global System Interrupt base (from MADT)
 * @param id I/O APIC ID (from MADT)
 * @return 0 on success, -1 on failure
 */
int ioapic_init(uint32_t phys_addr, uint32_t gsi_base, uint8_t id);

/**
 * Check if I/O APIC is available
 * @return true if I/O APIC is initialized
 */
bool ioapic_is_enabled(void);

/**
 * Get number of redirection entries
 * @return Number of interrupt inputs (typically 24)
 */
uint8_t ioapic_get_max_redirect(void);

/**
 * Map an IRQ to an interrupt vector
 * @param irq IRQ number (0-23)
 * @param vector Interrupt vector (0x20-0xFF)
 * @param apic_id Target Local APIC ID
 * @param flags Flags (trigger mode, polarity, etc.)
 */
void ioapic_map_irq(uint8_t irq, uint8_t vector, uint8_t apic_id, uint32_t flags);

/**
 * Mask (disable) an IRQ
 * @param irq IRQ number (0-23)
 */
void ioapic_mask_irq(uint8_t irq);

/**
 * Unmask (enable) an IRQ
 * @param irq IRQ number (0-23)
 */
void ioapic_unmask_irq(uint8_t irq);

/**
 * Read I/O APIC register
 * @param reg Register selector
 * @return Register value
 */
uint32_t ioapic_read(uint8_t reg);

/**
 * Write I/O APIC register
 * @param reg Register selector
 * @param value Value to write
 */
void ioapic_write(uint8_t reg, uint32_t value);

/**
 * Read redirection table entry (64-bit)
 * @param index Entry index (0-23)
 * @param low Pointer to store low 32 bits
 * @param high Pointer to store high 32 bits
 */
void ioapic_read_redtbl(uint8_t index, uint32_t *low, uint32_t *high);

/**
 * Write redirection table entry (64-bit)
 * @param index Entry index (0-23)
 * @param low Low 32 bits (vector, delivery mode, etc.)
 * @param high High 32 bits (destination APIC ID)
 */
void ioapic_write_redtbl(uint8_t index, uint32_t low, uint32_t high);

#endif /* IOAPIC_H */