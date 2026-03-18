/* ============================================================================
 * Interrupt Controller Manager
 * ============================================================================ */

#include "driver/apic/interrupt.h"
#include "driver/apic/lapic.h"
#include "driver/apic/ioapic.h"
#include "driver/pic.h"
#include "driver/acpi/acpi.h"
#include "driver/acpi/tables.h"
#include "kernel/asm.h"
#include "lib/printk.h"

/* ============================================================================
 * Global State
 * ============================================================================ */

static interrupt_mode_t g_int_mode = INT_MODE_UNKNOWN;

/* IRQ override table (from ACPI MADT) */
#define MAX_IRQ_OVERRIDES 16
static struct {
    uint8_t irq;        /* Source IRQ */
    uint8_t gsi;        /* Global System Interrupt */
    uint16_t flags;     /* Polarity/trigger mode */
} g_irq_overrides[MAX_IRQ_OVERRIDES];
static int g_override_count = 0;

/* ============================================================================
 * IRQ Override Handling
 * ============================================================================ */

static void register_irq_override(uint8_t irq, uint8_t gsi, uint16_t flags)
{
    if (g_override_count < MAX_IRQ_OVERRIDES) {
        g_irq_overrides[g_override_count].irq = irq;
        g_irq_overrides[g_override_count].gsi = gsi;
        g_irq_overrides[g_override_count].flags = flags;
        g_override_count++;
    }
}

static uint8_t irq_to_gsi(uint8_t irq)
{
    /* Check overrides */
    for (int i = 0; i < g_override_count; i++) {
        if (g_irq_overrides[i].irq == irq) {
            return g_irq_overrides[i].gsi;
        }
    }
    /* Identity mapping if no override */
    return irq;
}

/* ============================================================================
 * APIC Initialization from ACPI MADT
 * ============================================================================ */

static int init_apic_from_madt(void)
{
    extern acpi_madt_t *g_madt;  /* From acpi.c */
    
    if (!g_madt) {
        return -1;
    }
    
    printk("[INT] Initializing APIC mode from ACPI MADT...\n");
    
    /* Initialize Local APIC */
    if (lapic_init(g_madt->local_apic_address) != 0) {
        printk("[INT] Failed to initialize Local APIC\n");
        return -1;
    }
    
    /* Parse MADT entries */
    uint8_t *ptr = (uint8_t *)g_madt + sizeof(acpi_madt_t);
    uint8_t *end = (uint8_t *)g_madt + g_madt->header.length;
    
    int ioapic_found = 0;
    
    while (ptr < end) {
        acpi_madt_entry_header_t *entry = (acpi_madt_entry_header_t *)ptr;
        
        switch (entry->type) {
            case ACPI_MADT_TYPE_IO_APIC: {
                acpi_madt_io_apic_t *ioapic = (acpi_madt_io_apic_t *)ptr;
                
                if (ioapic_init(ioapic->io_apic_address,
                               ioapic->global_system_interrupt_base,
                               ioapic->io_apic_id) == 0) {
                    ioapic_found = 1;
                }
                break;
            }
            
            case ACPI_MADT_TYPE_INTERRUPT_OVERRIDE: {
                acpi_madt_interrupt_override_t *override = 
                    (acpi_madt_interrupt_override_t *)ptr;
                
                register_irq_override(override->source,
                                    override->global_system_interrupt,
                                    override->flags);
                
                printk("[INT] IRQ override: IRQ %u → GSI %u (flags=0x%04x)\n",
                       override->source, override->global_system_interrupt,
                       override->flags);
                break;
            }
        }
        
        ptr += entry->length;
    }
    
    if (!ioapic_found) {
        printk("[INT] No I/O APIC found in MADT\n");
        return -1;
    }
    
    /* Disable legacy PIC if present */
    if (g_madt->flags & ACPI_MADT_PCAT_COMPAT) {
        printk("[INT] Disabling legacy 8259 PIC...\n");
        pic_disable_all();
        
        /* Remap and mask PIC to prevent spurious interrupts */
        outb(0x21, 0xFF);  /* Master PIC */
        outb(0xA1, 0xFF);  /* Slave PIC */
    }
    
    printk("[INT] APIC initialization complete\n");
    
    /* Re-enable any IRQs that were enabled in PIC mode
     * Timer (IRQ0) and keyboard (IRQ1) are typically already enabled */
    printk("[INT] Setting up I/O APIC redirection entries...\n");
    
    /* Enable timer (IRQ0 -> GSI 2) */
    uint8_t gsi_timer = irq_to_gsi(0);
    ioapic_map_irq(gsi_timer, 0x20, lapic_get_id(), 
                   IOAPIC_DELMODE_FIXED | IOAPIC_DESTMODE_PHYSICAL);
    printk("[INT] Enabled timer: IRQ 0 -> GSI %u -> Vector 0x20\n", gsi_timer);
    
    /* Enable keyboard (IRQ1 -> GSI 1) */
    uint8_t gsi_kbd = irq_to_gsi(1);
    ioapic_map_irq(gsi_kbd, 0x21, lapic_get_id(),
                   IOAPIC_DELMODE_FIXED | IOAPIC_DESTMODE_PHYSICAL);
    printk("[INT] Enabled keyboard: IRQ 1 -> GSI %u -> Vector 0x21\n", gsi_kbd);
    
    return 0;
}

/* ============================================================================
 * Interrupt Controller Initialization
 * ============================================================================ */

void interrupt_init(void)
{
    printk("\n[INT] Initializing interrupt controller...\n");
    
    /* Try APIC first */
    if (acpi_is_available()) {
        if (init_apic_from_madt() == 0) {
            g_int_mode = INT_MODE_APIC;
            printk("[INT] Using APIC mode\n");
            return;
        }
    }
    
    /* Fallback to PIC */
    printk("[INT] No APIC available, using legacy PIC\n");
    pic_init(0x20, 0x28);
    g_int_mode = INT_MODE_PIC;
    printk("[INT] Using PIC mode\n");
}

/* ============================================================================
 * Interrupt Control Functions
 * ============================================================================ */

interrupt_mode_t interrupt_get_mode(void)
{
    return g_int_mode;
}

void interrupt_eoi(uint8_t irq)
{
    if (g_int_mode == INT_MODE_APIC) {
        lapic_send_eoi();
    } else {
        pic_send_eoi(irq);
    }
}

void interrupt_enable_irq(uint8_t irq)
{
    if (g_int_mode == INT_MODE_APIC) {
        uint8_t gsi = irq_to_gsi(irq);
        uint8_t vector = 0x20 + irq;
        uint8_t apic_id = lapic_get_id();
        
        /* Map IRQ with edge-triggered, active-high (default) */
        ioapic_map_irq(gsi, vector, apic_id, 
                      IOAPIC_DELMODE_FIXED | 
                      IOAPIC_DESTMODE_PHYSICAL);
    } else {
        pic_enable_irq(irq);
    }
}

void interrupt_disable_irq(uint8_t irq)
{
    if (g_int_mode == INT_MODE_APIC) {
        uint8_t gsi = irq_to_gsi(irq);
        ioapic_mask_irq(gsi);
    } else {
        pic_disable_irq(irq);
    }
}