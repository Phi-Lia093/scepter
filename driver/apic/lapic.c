/* ============================================================================
 * Local APIC Driver
 * ============================================================================ */

#include "driver/apic/lapic.h"
#include "kernel/asm.h"
#include "mm/vmalloc.h"
#include "lib/printk.h"
#include <stddef.h>

/* ============================================================================
 * Local APIC State
 * ============================================================================ */

static struct {
    volatile uint32_t *base;    /* Virtual address of APIC MMIO region */
    uint32_t phys_addr;         /* Physical address */
    uint8_t apic_id;            /* This CPU's APIC ID */
    uint8_t version;            /* APIC version */
    bool enabled;               /* True if APIC is active */
} g_lapic = {0};

/* ============================================================================
 * Local APIC Register Access
 * ============================================================================ */

uint32_t lapic_read(uint16_t reg)
{
    if (!g_lapic.base) {
        return 0;
    }
    return g_lapic.base[reg / 4];
}

void lapic_write(uint16_t reg, uint32_t value)
{
    if (!g_lapic.base) {
        return;
    }
    g_lapic.base[reg / 4] = value;
}

/* ============================================================================
 * Local APIC Initialization
 * ============================================================================ */

int lapic_init(uint32_t phys_addr)
{
    printk("[LAPIC] Initializing Local APIC at phys 0x%08x...\n", phys_addr);
    
    /* Map APIC MMIO region (4KB page) */
    g_lapic.base = (volatile uint32_t *)ioremap(phys_addr, 4096);
    if (!g_lapic.base) {
        printk("[LAPIC] ERROR: Failed to map APIC MMIO region\n");
        return -1;
    }
    
    g_lapic.phys_addr = phys_addr;
    
    /* Enable Local APIC via IA32_APIC_BASE MSR */
    uint64_t apic_base_msr = rdmsr(IA32_APIC_BASE);
    
    /* Check if this is the BSP (Bootstrap Processor) */
    if (apic_base_msr & APIC_BASE_BSP) {
        printk("[LAPIC] Running on Bootstrap Processor\n");
    }
    
    /* Set enable bit and ensure base address matches */
    apic_base_msr |= APIC_BASE_ENABLE;
    apic_base_msr = (apic_base_msr & ~APIC_BASE_ADDR_MASK) | phys_addr;
    wrmsr(IA32_APIC_BASE, apic_base_msr);
    
    /* Read APIC ID and version */
    g_lapic.apic_id = (lapic_read(LAPIC_ID) >> 24) & 0xFF;
    g_lapic.version = lapic_read(LAPIC_VERSION) & 0xFF;
    
    printk("[LAPIC] APIC ID: %u, Version: 0x%02x\n", 
           g_lapic.apic_id, g_lapic.version);
    
    /* Software enable APIC via Spurious Interrupt Vector Register */
    /* Set spurious vector to 0xFF and enable APIC */
    lapic_write(LAPIC_SIVR, LAPIC_SIVR_ENABLE | 0xFF);
    
    /* Clear and reset error status register (write 0, then read) */
    lapic_write(LAPIC_ESR, 0);
    lapic_write(LAPIC_ESR, 0);
    uint32_t esr = lapic_read(LAPIC_ESR);
    if (esr != 0) {
        printk("[LAPIC] WARNING: Error Status Register = 0x%08x\n", esr);
    }
    
    /* Set Task Priority to accept all interrupts */
    lapic_write(LAPIC_TPR, 0);
    
    /* Mask all LVT entries initially */
    lapic_write(LAPIC_LVT_TIMER, LAPIC_LVT_MASKED);
    lapic_write(LAPIC_LVT_THERMAL, LAPIC_LVT_MASKED);
    lapic_write(LAPIC_LVT_PERF, LAPIC_LVT_MASKED);
    lapic_write(LAPIC_LVT_LINT0, LAPIC_LVT_MASKED);
    lapic_write(LAPIC_LVT_LINT1, LAPIC_LVT_MASKED);
    lapic_write(LAPIC_LVT_ERROR, LAPIC_LVT_MASKED);
    
    /* Set Destination Format Register to Flat Model (all CPUs) */
    lapic_write(LAPIC_DFR, 0xFFFFFFFF);
    
    /* Set Logical Destination Register (for this CPU) */
    uint32_t ldr = lapic_read(LAPIC_LDR) & 0x00FFFFFF;
    lapic_write(LAPIC_LDR, ldr | (1 << 24));
    
    g_lapic.enabled = true;
    
    printk("[LAPIC] Initialized successfully\n");
    return 0;
}

/* ============================================================================
 * Local APIC Control Functions
 * ============================================================================ */

bool lapic_is_enabled(void)
{
    return g_lapic.enabled;
}

uint8_t lapic_get_id(void)
{
    return g_lapic.apic_id;
}

void lapic_send_eoi(void)
{
    if (g_lapic.enabled) {
        lapic_write(LAPIC_EOI, 0);
    }
}

void lapic_enable_lvt(uint16_t lvt_reg, uint8_t vector, uint32_t flags)
{
    if (!g_lapic.enabled) {
        return;
    }
    
    uint32_t value = vector | flags;
    lapic_write(lvt_reg, value);
}

void lapic_disable_lvt(uint16_t lvt_reg)
{
    if (!g_lapic.enabled) {
        return;
    }
    
    lapic_write(lvt_reg, LAPIC_LVT_MASKED);
}