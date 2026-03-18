/* ============================================================================
 * ACPI RSDP (Root System Description Pointer) Discovery
 * ============================================================================ */

#include "driver/acpi/acpi.h"
#include "lib/printk.h"
#include "lib/string.h"
#include "mm/mm.h"
#include "mm/vmalloc.h"

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
 * @return Physical address of RSDP, or 0 if not found
 */
static uint32_t search_rsdp_range(uint32_t start, uint32_t end)
{
    uint32_t size = end - start;
    
    /* Map the search range into virtual memory */
    uint8_t *mapped = (uint8_t *)ioremap(start, size);
    if (!mapped) {
        printk("[ACPI] Failed to map search range 0x%08x-0x%08x\n", start, end);
        return 0;
    }
    
    uint32_t rsdp_phys = 0;
    
    /* RSDP is aligned on 16-byte boundary */
    for (uint32_t offset = 0; offset < size; offset += 16) {
        /* Check for "RSD PTR " signature */
        if (memcmp(mapped + offset, "RSD PTR ", 8) == 0) {
            acpi_rsdp_t *rsdp = (acpi_rsdp_t *)(mapped + offset);
            
            /* Validate checksum */
            if (validate_rsdp_checksum(rsdp)) {
                rsdp_phys = start + offset;
                break;
            }
        }
    }
    
    /* Unmap the search range */
    iounmap(mapped);
    
    return rsdp_phys;
}

/**
 * Find RSDP in system memory
 * 
 * Search order (ACPI specification):
 * 1. EBDA (Extended BIOS Data Area) - first 1KB
 * 2. Main BIOS area (0xE0000 - 0xFFFFF)
 * 
 * @return Physical address of RSDP, or 0 if not found
 */
uint32_t acpi_find_rsdp_phys(void)
{
    printk("[ACPI] Searching for RSDP...\n");
    
    /* Get EBDA address from BDA (BIOS Data Area) at 0x40E */
    uint16_t *ebda_ptr = (uint16_t *)PHYS_TO_VIRT(0x40E);
    uint32_t ebda_addr = (*ebda_ptr) << 4;  /* Segment to physical address */
    
    uint32_t rsdp_phys = 0;
    
    if (ebda_addr != 0) {
        printk("[ACPI]   Searching EBDA at 0x%08x...\n", ebda_addr);
        rsdp_phys = search_rsdp_range(ebda_addr, ebda_addr + 1024);
        if (rsdp_phys) {
            printk("[ACPI]   Found RSDP in EBDA at phys 0x%08x\n", rsdp_phys);
            return rsdp_phys;
        }
    }
    
    /* Search main BIOS area (0xE0000 - 0xFFFFF) */
    printk("[ACPI]   Searching BIOS ROM area 0xE0000-0xFFFFF...\n");
    rsdp_phys = search_rsdp_range(0xE0000, 0x100000);
    if (rsdp_phys) {
        printk("[ACPI]   Found RSDP in BIOS ROM at phys 0x%08x\n", rsdp_phys);
        return rsdp_phys;
    }
    
    printk("[ACPI]   RSDP not found\n");
    return 0;
}

/**
 * Map RSDP into virtual memory
 * @param rsdp_phys Physical address of RSDP
 * @return Virtual pointer to RSDP, or NULL if failed
 */
acpi_rsdp_t* acpi_find_rsdp(void)
{
    uint32_t rsdp_phys = acpi_find_rsdp_phys();
    if (rsdp_phys == 0) {
        return NULL;
    }
    
    /* Map RSDP into virtual memory (permanently for now) */
    return (acpi_rsdp_t *)ioremap(rsdp_phys, sizeof(acpi_rsdp_t));
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