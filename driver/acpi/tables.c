/* ============================================================================
 * ACPI Table Parsing
 * ============================================================================ */

#include "driver/acpi/acpi.h"
#include "driver/acpi/tables.h"
#include "lib/printk.h"
#include "lib/string.h"
#include "mm/mm.h"

/* ============================================================================
 * Checksum Validation
 * ============================================================================ */

/**
 * Validate ACPI table checksum
 * @param table Pointer to table
 * @param length Length of table
 * @return 1 if valid, 0 if invalid
 */
int acpi_validate_checksum(void *table, uint32_t length)
{
    uint8_t *ptr = (uint8_t *)table;
    uint8_t sum = 0;
    
    for (uint32_t i = 0; i < length; i++) {
        sum += ptr[i];
    }
    
    return (sum == 0);
}

/* ============================================================================
 * Table Discovery
 * ============================================================================ */

/**
 * Find a table by signature in RSDT
 * @param rsdt Pointer to RSDT
 * @param signature 4-character signature (e.g., "FACP", "APIC")
 * @return Physical address of table, or 0 if not found
 */
uint32_t acpi_find_table(acpi_rsdt_t *rsdt, const char *signature)
{
    if (!rsdt) {
        return 0;
    }
    
    /* Calculate number of entries */
    uint32_t entries = (rsdt->header.length - sizeof(acpi_sdt_header_t)) / 4;
    
    /* Search through all entries */
    for (uint32_t i = 0; i < entries; i++) {
        uint32_t phys_addr = rsdt->entries[i];
        
        /* Map physical address to virtual */
        acpi_sdt_header_t *header = (acpi_sdt_header_t *)PHYS_TO_VIRT(phys_addr);
        
        /* Compare signature */
        if (memcmp(header->signature, signature, 4) == 0) {
            return phys_addr;
        }
    }
    
    return 0;
}

/* ============================================================================
 * MADT (APIC) Parsing
 * ============================================================================ */

/**
 * Parse and display MADT entries
 * @param madt Pointer to MADT table
 */
void acpi_parse_madt(acpi_madt_t *madt)
{
    if (!madt) {
        return;
    }
    
    printk("[ACPI] MADT (Multiple APIC Description Table):\n");
    printk("[ACPI]   Local APIC Address: 0x%08x\n", madt->local_apic_address);
    printk("[ACPI]   Flags: 0x%08x%s\n", madt->flags,
           (madt->flags & ACPI_MADT_PCAT_COMPAT) ? " (8259 PICs present)" : "");
    
    /* Parse variable-length entries */
    uint8_t *ptr = (uint8_t *)madt + sizeof(acpi_madt_t);
    uint8_t *end = (uint8_t *)madt + madt->header.length;
    
    int cpu_count = 0;
    int ioapic_count = 0;
    int override_count = 0;
    
    while (ptr < end) {
        acpi_madt_entry_header_t *entry = (acpi_madt_entry_header_t *)ptr;
        
        switch (entry->type) {
            case ACPI_MADT_TYPE_LOCAL_APIC: {
                acpi_madt_local_apic_t *lapic = (acpi_madt_local_apic_t *)ptr;
                if (lapic->flags & 1) {  /* Enabled */
                    printk("[ACPI]   CPU %u: ACPI Processor ID=%u, APIC ID=%u%s\n",
                           cpu_count, lapic->acpi_processor_id, lapic->apic_id,
                           (lapic->flags & 1) ? " (enabled)" : " (disabled)");
                    cpu_count++;
                }
                break;
            }
            
            case ACPI_MADT_TYPE_IO_APIC: {
                acpi_madt_io_apic_t *ioapic = (acpi_madt_io_apic_t *)ptr;
                printk("[ACPI]   I/O APIC %u: ID=%u, Address=0x%08x, GSI Base=%u\n",
                       ioapic_count, ioapic->io_apic_id, ioapic->io_apic_address,
                       ioapic->global_system_interrupt_base);
                ioapic_count++;
                break;
            }
            
            case ACPI_MADT_TYPE_INTERRUPT_OVERRIDE: {
                acpi_madt_interrupt_override_t *override = 
                    (acpi_madt_interrupt_override_t *)ptr;
                printk("[ACPI]   Interrupt Override %u: IRQ %u -> GSI %u (flags=0x%04x)\n",
                       override_count, override->source, 
                       override->global_system_interrupt, override->flags);
                override_count++;
                break;
            }
            
            case ACPI_MADT_TYPE_LOCAL_APIC_NMI: {
                printk("[ACPI]   Local APIC NMI entry\n");
                break;
            }
            
            default:
                printk("[ACPI]   Unknown MADT entry type %u (length %u)\n",
                       entry->type, entry->length);
                break;
        }
        
        ptr += entry->length;
    }
    
    printk("[ACPI] MADT Summary: %u CPUs, %u I/O APICs, %u IRQ overrides\n",
           cpu_count, ioapic_count, override_count);
}

/* ============================================================================
 * Table Information Display
 * ============================================================================ */

/**
 * Display information about a table
 * @param header Pointer to table header
 */
void acpi_print_table_info(acpi_sdt_header_t *header)
{
    if (!header) {
        return;
    }
    
    /* Print signature (null-terminate for safety) */
    char sig[5];
    memcpy(sig, header->signature, 4);
    sig[4] = '\0';
    
    /* Print OEM IDs */
    char oem_id[7];
    char oem_table_id[9];
    memcpy(oem_id, header->oem_id, 6);
    memcpy(oem_table_id, header->oem_table_id, 8);
    oem_id[6] = '\0';
    oem_table_id[8] = '\0';
    
    printk("[ACPI] Table: %s (Length=%u, Revision=%u, OEM=%s, OEM Table=%s)\n",
           sig, header->length, header->revision, oem_id, oem_table_id);
}

/**
 * List all tables in RSDT
 * @param rsdt Pointer to RSDT
 */
void acpi_list_tables(acpi_rsdt_t *rsdt)
{
    if (!rsdt) {
        return;
    }
    
    printk("[ACPI] RSDT contains the following tables:\n");
    
    /* Calculate number of entries */
    uint32_t entries = (rsdt->header.length - sizeof(acpi_sdt_header_t)) / 4;
    
    /* List all tables */
    for (uint32_t i = 0; i < entries; i++) {
        uint32_t phys_addr = rsdt->entries[i];
        acpi_sdt_header_t *header = (acpi_sdt_header_t *)PHYS_TO_VIRT(phys_addr);
        
        char sig[5];
        memcpy(sig, header->signature, 4);
        sig[4] = '\0';
        
        printk("[ACPI]   [%u] %s at 0x%08x (length=%u bytes)\n",
               i, sig, phys_addr, header->length);
    }
}