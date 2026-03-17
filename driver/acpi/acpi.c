/* ============================================================================
 * ACPI (Advanced Configuration and Power Interface) Driver
 * 
 * Main coordination module for ACPI subsystem.
 * Implements ACPI 1.0 features: table discovery, shutdown, device enumeration.
 * ============================================================================ */

#include "driver/acpi/acpi.h"
#include "driver/acpi/tables.h"
#include "lib/printk.h"
#include "mm/mm.h"
#include <stddef.h>

/* External functions from other ACPI modules */
extern acpi_rsdp_t* acpi_find_rsdp(void);
extern void acpi_print_rsdp_info(acpi_rsdp_t *rsdp);
extern void acpi_parse_fadt_for_pm(acpi_fadt_t *fadt);
extern void acpi_parse_madt(acpi_madt_t *madt);
extern void acpi_list_tables(acpi_rsdt_t *rsdt);
extern int acpi_pm_is_available(void);

/* Global ACPI state */
static acpi_rsdp_t *g_rsdp = NULL;
static acpi_rsdt_t *g_rsdt = NULL;
static acpi_fadt_t *g_fadt = NULL;
static acpi_madt_t *g_madt = NULL;
static int g_acpi_available = 0;

/* ============================================================================
 * ACPI Initialization
 * ============================================================================ */

/**
 * Initialize ACPI subsystem
 * 
 * Steps:
 * 1. Find RSDP
 * 2. Locate and validate RSDT
 * 3. Find and parse FADT (for power management)
 * 4. Find and parse MADT (for device enumeration)
 */
void acpi_init(void)
{
    printk("\n");
    printk("========================================\n");
    printk("  ACPI Initialization\n");
    printk("========================================\n");
    
    /* Step 1: Find RSDP */
    g_rsdp = acpi_find_rsdp();
    if (!g_rsdp) {
        printk("[ACPI] FATAL: RSDP not found!\n");
        printk("[ACPI] ACPI not available on this system\n");
        return;
    }
    
    acpi_print_rsdp_info(g_rsdp);
    
    /* Step 2: Locate RSDT */
    uint32_t rsdt_phys = g_rsdp->rsdt_address;
    g_rsdt = (acpi_rsdt_t *)PHYS_TO_VIRT(rsdt_phys);
    
    /* Validate RSDT */
    if (!acpi_validate_checksum(g_rsdt, g_rsdt->header.length)) {
        printk("[ACPI] FATAL: RSDT checksum validation failed!\n");
        g_rsdp = NULL;
        g_rsdt = NULL;
        return;
    }
    
    printk("[ACPI] RSDT validated successfully\n");
    
    /* List all available tables */
    acpi_list_tables(g_rsdt);
    printk("\n");
    
    /* Step 3: Find and parse FADT */
    uint32_t fadt_phys = acpi_find_table(g_rsdt, "FACP");
    if (fadt_phys) {
        g_fadt = (acpi_fadt_t *)PHYS_TO_VIRT(fadt_phys);
        
        if (acpi_validate_checksum(g_fadt, g_fadt->header.length)) {
            printk("[ACPI] FADT found and validated\n");
            acpi_parse_fadt_for_pm(g_fadt);
            printk("\n");
        } else {
            printk("[ACPI] WARNING: FADT checksum validation failed\n");
            g_fadt = NULL;
        }
    } else {
        printk("[ACPI] WARNING: FADT not found (shutdown may not work)\n");
    }
    
    /* Step 4: Find and parse MADT */
    uint32_t madt_phys = acpi_find_table(g_rsdt, "APIC");
    if (madt_phys) {
        g_madt = (acpi_madt_t *)PHYS_TO_VIRT(madt_phys);
        
        if (acpi_validate_checksum(g_madt, g_madt->header.length)) {
            printk("[ACPI] MADT found and validated\n");
            /* Don't parse yet - wait for explicit enumeration call */
        } else {
            printk("[ACPI] WARNING: MADT checksum validation failed\n");
            g_madt = NULL;
        }
    } else {
        printk("[ACPI] WARNING: MADT not found (no APIC information)\n");
    }
    
    /* Mark ACPI as available */
    g_acpi_available = 1;
    
    printk("========================================\n");
    printk("[ACPI] Initialization complete\n");
    printk("[ACPI] Status: %s\n", 
           acpi_pm_is_available() ? "Power management ready" : "Limited functionality");
    printk("========================================\n\n");
}

/* ============================================================================
 * Device Enumeration
 * ============================================================================ */

/**
 * Enumerate devices from ACPI tables
 * Parses MADT to display CPU and I/O APIC information
 */
void acpi_enum_devices(void)
{
    if (!g_acpi_available) {
        printk("[ACPI] Device enumeration failed: ACPI not initialized\n");
        return;
    }
    
    printk("\n");
    printk("========================================\n");
    printk("  ACPI Device Enumeration\n");
    printk("========================================\n");
    
    if (g_madt) {
        acpi_parse_madt(g_madt);
    } else {
        printk("[ACPI] No MADT available - cannot enumerate devices\n");
    }
    
    printk("========================================\n\n");
}

/* ============================================================================
 * Status Functions
 * ============================================================================ */

/**
 * Check if ACPI is available
 * @return 1 if ACPI initialized successfully, 0 otherwise
 */
int acpi_is_available(void)
{
    return g_acpi_available;
}