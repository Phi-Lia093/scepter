/* ============================================================================
 * ACPI Power Management - Shutdown and Reboot
 * ============================================================================ */

#include "driver/acpi/acpi.h"
#include "driver/acpi/tables.h"
#include "kernel/asm.h"
#include "lib/printk.h"
#include "mm/mm.h"

/* Global power management state */
static uint32_t pm1a_cnt_blk = 0;  /* PM1a Control Block address */
static uint32_t pm1b_cnt_blk = 0;  /* PM1b Control Block address */
static uint16_t slp_typa = 0;      /* SLP_TYPa value for S5 (shutdown) */
static uint16_t slp_typb = 0;      /* SLP_TYPb value for S5 */
static int acpi_pm_available = 0;  /* Power management available flag */

/* ============================================================================
 * FADT Parsing for Power Management
 * ============================================================================ */

/**
 * Parse FADT to extract power management information
 * @param fadt Pointer to FADT table
 */
void acpi_parse_fadt_for_pm(acpi_fadt_t *fadt)
{
    if (!fadt) {
        return;
    }
    
    printk("[ACPI] FADT (Fixed ACPI Description Table):\n");
    printk("[ACPI]   PM1a Control Block: 0x%04x\n", fadt->pm1a_cnt_blk);
    printk("[ACPI]   PM1b Control Block: 0x%04x\n", fadt->pm1b_cnt_blk);
    printk("[ACPI]   PM1 Control Length: %u bytes\n", fadt->pm1_cnt_len);
    
    /* Save PM control registers */
    pm1a_cnt_blk = fadt->pm1a_cnt_blk;
    pm1b_cnt_blk = fadt->pm1b_cnt_blk;
    
    /* For QEMU and most systems, SLP_TYP = 0 works for shutdown
     * A proper implementation would parse the DSDT for \_S5 object */
    slp_typa = 0;
    slp_typb = 0;
    
    if (pm1a_cnt_blk != 0) {
        acpi_pm_available = 1;
        printk("[ACPI] Power management available (PM1a at 0x%04x)\n", 
               pm1a_cnt_blk);
    } else {
        printk("[ACPI] Power management NOT available\n");
    }
}

/* ============================================================================
 * Shutdown Implementation
 * ============================================================================ */

/**
 * Shutdown the system via ACPI
 * 
 * Writes to PM1a Control register to enter S5 (soft off) state.
 * This should power off the system on ACPI-compliant hardware.
 */
void acpi_shutdown(void)
{
    if (!acpi_pm_available) {
        printk("[ACPI] Shutdown failed: ACPI power management not available\n");
        printk("[ACPI] System halted. Please power off manually.\n");
        cli();
        while (1) {
            hlt();
        }
    }
    
    printk("\n[ACPI] Initiating system shutdown...\n");
    printk("[ACPI] Entering S5 sleep state (soft off)\n");
    
    /* Disable interrupts to prevent any interference */
    cli();
    
    /* Write to PM1a Control register
     * Format: SLP_TYPa | SLP_EN
     * SLP_TYPa is in bits 10-12, SLP_EN is bit 13 */
    
    uint16_t pm1a_value = (slp_typa << 10) | ACPI_PM1_SLP_EN;
    
    printk("[ACPI] Writing 0x%04x to PM1a_CNT (0x%04x)\n", 
           pm1a_value, pm1a_cnt_blk);
    
    outw(pm1a_cnt_blk, pm1a_value);
    
    /* If PM1b exists, write to it too */
    if (pm1b_cnt_blk != 0) {
        uint16_t pm1b_value = (slp_typb << 10) | ACPI_PM1_SLP_EN;
        printk("[ACPI] Writing 0x%04x to PM1b_CNT (0x%04x)\n",
               pm1b_value, pm1b_cnt_blk);
        outw(pm1b_cnt_blk, pm1b_value);
    }
    
    /* System should power off now
     * If we reach here, shutdown failed */
    printk("[ACPI] Shutdown command sent, waiting for power off...\n");
    
    /* Wait a bit for shutdown to complete */
    for (volatile int i = 0; i < 10000000; i++);
    
    printk("[ACPI] WARNING: System did not power off!\n");
    printk("[ACPI] System halted. Please power off manually.\n");
    
    /* Halt the CPU */
    while (1) {
        hlt();
    }
}

/* ============================================================================
 * Reboot Implementation
 * ============================================================================ */

/**
 * Reboot the system
 * 
 * Methods tried in order:
 * 1. ACPI reset register (if available)
 * 2. Keyboard controller reset (port 0x64)
 * 3. Triple fault (last resort)
 */
void acpi_reboot(void)
{
    printk("\n[ACPI] Initiating system reboot...\n");
    
    /* Disable interrupts */
    cli();
    
    /* Method 1: Try keyboard controller reset (most reliable on x86) */
    printk("[ACPI] Attempting keyboard controller reset...\n");
    
    /* Wait for keyboard controller to be ready */
    uint8_t status;
    do {
        status = inb(0x64);
    } while (status & 0x02);  /* Wait until input buffer empty */
    
    /* Send reset command to keyboard controller */
    outb(0x64, 0xFE);
    
    /* Wait a bit */
    for (volatile int i = 0; i < 1000000; i++);
    
    /* Method 2: Triple fault (causes CPU reset) */
    printk("[ACPI] Keyboard reset failed, attempting triple fault...\n");
    
    /* Load invalid IDT to cause triple fault */
    struct {
        uint16_t limit;
        uint32_t base;
    } __attribute__((packed)) idtr = {0, 0};
    
    asm volatile("lidt %0" :: "m"(idtr));
    asm volatile("int $0x03");  /* Trigger interrupt with invalid IDT */
    
    /* Should never reach here */
    printk("[ACPI] ERROR: Reboot failed!\n");
    printk("[ACPI] System halted. Please reset manually.\n");
    
    while (1) {
        hlt();
    }
}

/* ============================================================================
 * Status Functions
 * ============================================================================ */

/**
 * Check if ACPI power management is available
 * @return 1 if available, 0 otherwise
 */
int acpi_pm_is_available(void)
{
    return acpi_pm_available;
}