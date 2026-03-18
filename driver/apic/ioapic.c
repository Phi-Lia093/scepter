/* ============================================================================
 * I/O APIC Driver
 * ============================================================================ */

#include "driver/apic/ioapic.h"
#include "mm/vmalloc.h"
#include "lib/printk.h"
#include <stddef.h>

/* ============================================================================
 * I/O APIC State
 * ============================================================================ */

static struct {
    volatile uint32_t *base;    /* Virtual address of I/O APIC MMIO region */
    uint32_t phys_addr;         /* Physical address */
    uint8_t id;                 /* I/O APIC ID */
    uint8_t version;            /* I/O APIC version */
    uint8_t max_redirect;       /* Maximum redirection entries */
    uint32_t gsi_base;          /* Global System Interrupt base */
    bool enabled;               /* True if I/O APIC is active */
} g_ioapic = {0};

/* ============================================================================
 * I/O APIC Register Access
 *
 * I/O APIC uses indirect register access:
 * - Write register selector to IOREGSEL (offset 0x00)
 * - Read/write data from/to IOWIN (offset 0x10)
 * ============================================================================ */

#define IOREGSEL 0x00
#define IOWIN    0x10

uint32_t ioapic_read(uint8_t reg)
{
    if (!g_ioapic.base) {
        return 0;
    }
    
    /* Write register selector */
    g_ioapic.base[IOREGSEL / 4] = reg;
    
    /* Read data */
    return g_ioapic.base[IOWIN / 4];
}

void ioapic_write(uint8_t reg, uint32_t value)
{
    if (!g_ioapic.base) {
        return;
    }
    
    /* Write register selector */
    g_ioapic.base[IOREGSEL / 4] = reg;
    
    /* Write data */
    g_ioapic.base[IOWIN / 4] = value;
}

/* ============================================================================
 * Redirection Table Access (64-bit entries)
 * ============================================================================ */

void ioapic_read_redtbl(uint8_t index, uint32_t *low, uint32_t *high)
{
    if (index >= g_ioapic.max_redirect) {
        return;
    }
    
    *low = ioapic_read(IOAPIC_REDTBL_BASE + (index * 2));
    *high = ioapic_read(IOAPIC_REDTBL_BASE + (index * 2) + 1);
}

void ioapic_write_redtbl(uint8_t index, uint32_t low, uint32_t high)
{
    if (index >= g_ioapic.max_redirect) {
        return;
    }
    
    ioapic_write(IOAPIC_REDTBL_BASE + (index * 2), low);
    ioapic_write(IOAPIC_REDTBL_BASE + (index * 2) + 1, high);
}

/* ============================================================================
 * I/O APIC Initialization
 * ============================================================================ */

int ioapic_init(uint32_t phys_addr, uint32_t gsi_base, uint8_t id)
{
    printk("[IOAPIC] Initializing I/O APIC %u at phys 0x%08x, GSI base %u...\n",
           id, phys_addr, gsi_base);
    
    /* Map I/O APIC MMIO region (4KB page) */
    g_ioapic.base = (volatile uint32_t *)ioremap(phys_addr, 4096);
    if (!g_ioapic.base) {
        printk("[IOAPIC] ERROR: Failed to map I/O APIC MMIO region\n");
        return -1;
    }
    
    g_ioapic.phys_addr = phys_addr;
    g_ioapic.id = id;
    g_ioapic.gsi_base = gsi_base;
    
    /* Read version register to get max redirection entries */
    uint32_t ver = ioapic_read(IOAPIC_REG_VER);
    g_ioapic.version = ver & 0xFF;
    g_ioapic.max_redirect = ((ver >> 16) & 0xFF) + 1;
    
    printk("[IOAPIC] Version: 0x%02x, Max redirect entries: %u\n",
           g_ioapic.version, g_ioapic.max_redirect);
    
    /* Set I/O APIC ID (optional, firmware usually sets this) */
    uint32_t current_id = ioapic_read(IOAPIC_REG_ID);
    printk("[IOAPIC] Current ID: 0x%08x, Setting to: %u\n",
           current_id, id);
    ioapic_write(IOAPIC_REG_ID, (uint32_t)id << 24);
    
    /* Mask all interrupts initially */
    for (int i = 0; i < g_ioapic.max_redirect; i++) {
        ioapic_write_redtbl(i, IOAPIC_MASKED, 0);
    }
    
    g_ioapic.enabled = true;
    
    printk("[IOAPIC] Initialized successfully\n");
    return 0;
}

/* ============================================================================
 * I/O APIC Control Functions
 * ============================================================================ */

bool ioapic_is_enabled(void)
{
    return g_ioapic.enabled;
}

uint8_t ioapic_get_max_redirect(void)
{
    return g_ioapic.max_redirect;
}

void ioapic_map_irq(uint8_t irq, uint8_t vector, uint8_t apic_id, uint32_t flags)
{
    if (!g_ioapic.enabled || irq >= g_ioapic.max_redirect) {
        return;
    }
    
    /* Build redirection table entry */
    uint32_t low = vector | flags;
    uint32_t high = ((uint32_t)apic_id) << 24;
    
    /* Write to redirection table */
    ioapic_write_redtbl(irq, low, high);
    
    printk("[IOAPIC] Mapped IRQ %u → Vector 0x%02x (APIC ID %u)\n",
           irq, vector, apic_id);
}

void ioapic_mask_irq(uint8_t irq)
{
    if (!g_ioapic.enabled || irq >= g_ioapic.max_redirect) {
        return;
    }
    
    uint32_t low, high;
    ioapic_read_redtbl(irq, &low, &high);
    low |= IOAPIC_MASKED;
    ioapic_write_redtbl(irq, low, high);
}

void ioapic_unmask_irq(uint8_t irq)
{
    if (!g_ioapic.enabled || irq >= g_ioapic.max_redirect) {
        return;
    }
    
    uint32_t low, high;
    ioapic_read_redtbl(irq, &low, &high);
    low &= ~IOAPIC_MASKED;
    ioapic_write_redtbl(irq, low, high);
}