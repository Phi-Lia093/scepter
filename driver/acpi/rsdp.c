/* ============================================================================
 * ACPI RSDP (Root System Description Pointer) Discovery
 * ============================================================================ */

#include "driver/acpi/acpi.h"
#include "lib/printk.h"
#include "lib/string.h"
#include "mm/mm.h"

/* ============================================================================
 * RSDP Search Functions
 * ============================================================================ */

/**
 * Validate RSDP checksum
 * @param rsdp Pointer to RSDP structure
 * @return 1 if valid, 0 if invalid
 */
static int validate_rsdp_checksum(acpi_rsdp_t *rsdp)
{
    uint8_t *ptr = (uint8_t *)rsdp;
    uint8_t sum = 0;
    
    /* Sum first 20 bytes (ACPI 1.0 RSDP size) */
    for (int i = 0; i < 20; i++) {
        sum += ptr[i];
    }
    
    return (sum == 0);
}

/**
 * Search for RSDP in a memory range
 * @param start Physical start address
 * @param end Physical end address (exclusive)
 * @return Pointer to RSDP, or NULL if not found
 */
static acpi_rsdp_t* search_rsdp_range(uint32_t start, uint32_t end)
{
    /* Convert physical address to virtual (direct-mapped region) */
    uint8_t *ptr = (uint8_t *)PHYS_TO_VIRT(start);
    uint8_t *end_ptr = (uint8_t *)PHYS_TO_VIRT(end);
    
    /* RSDP is aligned on 16-byte boundary */
    while (ptr < end_ptr) {
        /* Check for "RSD PTR " signature */
        if (memcmp(ptr, "RSD PTR ", 8) == 0) {
            acpi_rsdp_t *rsdp = (acpi_rsdp_t *)ptr;
            
            /* Validate checksum */
            if (validate_rsdp_checksum(rsdp)) {
                return rsdp;
            }
        }
        
        ptr += 16;  /* Next 16-byte boundary */
    }
    
    return NULL;
}

/**
 * Find RSDP in system memory
 * 
 * Search order (ACPI specification):
 * 1. EBDA (Extended BIOS Data Area) - first 1KB
 * 2. Main BIOS area (0xE0000 - 0xFFFFF)
 * 
 * @return Pointer to RSDP, or NULL if not found
 */
acpi_rsdp_t* acpi_find_rsdp(void)
{
    acpi_rsdp_t *rsdp = NULL;
    
    printk("[ACPI] Searching for RSDP...\n");
    
    /* Get EBDA address from BDA (BIOS Data Area) at 0x40E */
    uint16_t *ebda_ptr = (uint16_t *)PHYS_TO_VIRT(0x40E);
    uint32_t ebda_addr = (*ebda_ptr) << 4;  /* Segment to physical address */
    
    if (ebda_addr != 0) {
        printk("[ACPI]   Searching EBDA at 0x%08x...\n", ebda_addr);
        rsdp = search_rsdp_range(ebda_addr, ebda_addr + 1024);
        if (rsdp) {
            printk("[ACPI]   Found RSDP in EBDA at 0x%08x\n", 
                   (uint32_t)rsdp - KERNEL_VMA);
            return rsdp;
        }
    }
    
    /* Search main BIOS area (0xE0000 - 0xFFFFF) */
    printk("[ACPI]   Searching BIOS ROM area 0xE0000-0xFFFFF...\n");
    rsdp = search_rsdp_range(0xE0000, 0x100000);
    if (rsdp) {
        printk("[ACPI]   Found RSDP in BIOS ROM at 0x%08x\n",
               (uint32_t)rsdp - KERNEL_VMA);
        return rsdp;
    }
    
    printk("[ACPI]   RSDP not found\n");
    return NULL;
}

/**
 * Display RSDP information
 * @param rsdp Pointer to RSDP
 */
void acpi_print_rsdp_info(acpi_rsdp_t *rsdp)
{
    if (!rsdp) {
        return;
    }
    
    /* Print OEM ID (null-terminate for safety) */
    char oem_id[7];
    memcpy(oem_id, rsdp->oem_id, 6);
    oem_id[6] = '\0';
    
    printk("[ACPI] RSDP Information:\n");
    printk("[ACPI]   OEM ID: %s\n", oem_id);
    printk("[ACPI]   Revision: %u (ACPI %s)\n", 
           rsdp->revision, rsdp->revision == 0 ? "1.0" : "2.0+");
    printk("[ACPI]   RSDT Address: 0x%08x\n", rsdp->rsdt_address);
}