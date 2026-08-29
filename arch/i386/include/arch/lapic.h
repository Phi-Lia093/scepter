#ifndef LAPIC_H
#define LAPIC_H

#include <stdint.h>
#include <stdbool.h>

/* ============================================================================
 * Local APIC (Advanced Programmable Interrupt Controller)
 *
 * Each CPU core has its own Local APIC for receiving and handling interrupts.
 * Memory-mapped at 0xFEE00000 (default) or configured via IA32_APIC_BASE MSR.
 * ============================================================================ */

/* Local APIC Register Offsets (from base address) */
#define LAPIC_ID            0x020   /* APIC ID Register */
#define LAPIC_VERSION       0x030   /* APIC Version Register */
#define LAPIC_TPR           0x080   /* Task Priority Register */
#define LAPIC_APR           0x090   /* Arbitration Priority Register */
#define LAPIC_PPR           0x0A0   /* Processor Priority Register */
#define LAPIC_EOI           0x0B0   /* End Of Interrupt Register */
#define LAPIC_RRD           0x0C0   /* Remote Read Register */
#define LAPIC_LDR           0x0D0   /* Logical Destination Register */
#define LAPIC_DFR           0x0E0   /* Destination Format Register */
#define LAPIC_SIVR          0x0F0   /* Spurious Interrupt Vector Register */
#define LAPIC_ISR_BASE      0x100   /* In-Service Register (8 registers) */
#define LAPIC_TMR_BASE      0x180   /* Trigger Mode Register (8 registers) */
#define LAPIC_IRR_BASE      0x200   /* Interrupt Request Register (8 registers) */
#define LAPIC_ESR           0x280   /* Error Status Register */
#define LAPIC_ICR_LOW       0x300   /* Interrupt Command Register (low) */
#define LAPIC_ICR_HIGH      0x310   /* Interrupt Command Register (high) */
#define LAPIC_LVT_TIMER     0x320   /* LVT Timer Register */
#define LAPIC_LVT_THERMAL   0x330   /* LVT Thermal Sensor Register */
#define LAPIC_LVT_PERF      0x340   /* LVT Performance Monitor Register */
#define LAPIC_LVT_LINT0     0x350   /* LVT LINT0 Register */
#define LAPIC_LVT_LINT1     0x360   /* LVT LINT1 Register */
#define LAPIC_LVT_ERROR     0x370   /* LVT Error Register */
#define LAPIC_TIMER_ICR     0x380   /* Timer Initial Count Register */
#define LAPIC_TIMER_CCR     0x390   /* Timer Current Count Register */
#define LAPIC_TIMER_DCR     0x3E0   /* Timer Divide Configuration Register */

/* APIC Base MSR bits */
#define APIC_BASE_BSP       (1 << 8)    /* Bootstrap Processor */
#define APIC_BASE_ENABLE    (1 << 11)   /* APIC Enable */
#define APIC_BASE_ADDR_MASK 0xFFFFF000  /* Base address mask */

/* SIVR bits */
#define LAPIC_SIVR_ENABLE   (1 << 8)    /* APIC Software Enable */
#define LAPIC_SIVR_FOCUS    (1 << 9)    /* Focus Processor Checking */

/* LVT Entry bits */
#define LAPIC_LVT_MASKED    (1 << 16)   /* Interrupt masked */
#define LAPIC_LVT_LEVEL     (1 << 15)   /* Level triggered (vs edge) */
#define LAPIC_LVT_ACTIVE_LOW (1 << 13)  /* Active low (vs high) */
#define LAPIC_LVT_PENDING   (1 << 12)   /* Delivery status pending */

/* Delivery modes for LVT entries */
#define LAPIC_DELMODE_FIXED     0x000   /* Fixed delivery */
#define LAPIC_DELMODE_SMI       0x200   /* SMI */
#define LAPIC_DELMODE_NMI       0x400   /* NMI */
#define LAPIC_DELMODE_INIT      0x500   /* INIT */
#define LAPIC_DELMODE_EXTINT    0x700   /* External interrupt */

/* ============================================================================
 * Function Prototypes
 * ============================================================================ */

/**
 * Initialize Local APIC
 * @param phys_addr Physical address of Local APIC (from MADT)
 * @return 0 on success, -1 on failure
 */
int lapic_init(uint32_t phys_addr);

/**
 * Check if Local APIC is available
 * @return true if LAPIC is initialized and enabled
 */
bool lapic_is_enabled(void);

/**
 * Get this CPU's APIC ID
 * @return APIC ID (0-255)
 */
uint8_t lapic_get_id(void);

/**
 * Send End-Of-Interrupt to Local APIC
 * Must be called at the end of every interrupt handler when using APIC
 */
void lapic_send_eoi(void);

/**
 * Enable specific LVT entry
 * @param lvt_reg LVT register offset (e.g., LAPIC_LVT_TIMER)
 * @param vector Interrupt vector (0-255)
 * @param flags Flags (delivery mode, masked, etc.)
 */
void lapic_enable_lvt(uint16_t lvt_reg, uint8_t vector, uint32_t flags);

/**
 * Disable specific LVT entry
 * @param lvt_reg LVT register offset
 */
void lapic_disable_lvt(uint16_t lvt_reg);

/**
 * Read Local APIC register
 * @param reg Register offset
 * @return Register value
 */
uint32_t lapic_read(uint16_t reg);

/**
 * Write Local APIC register
 * @param reg Register offset
 * @param value Value to write
 */
void lapic_write(uint16_t reg, uint32_t value);

#endif /* LAPIC_H */