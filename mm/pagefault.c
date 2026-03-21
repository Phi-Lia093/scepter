/* ============================================================================
 * Page Fault Handler - Demand Paging Implementation
 * ============================================================================ */

#include "mm/pagefault.h"
#include "mm/vma.h"
#include "mm/pgtable.h"
#include "mm/buddy.h"
#include "mm/mm.h"
#include "kernel/sched.h"
#include "kernel/panic.h"
#include "lib/printk.h"
#include "lib/string.h"

#define PAGE_SIZE 4096
#define PAGE_ALIGN_DOWN(addr) ((addr) & ~(PAGE_SIZE - 1))

/* ============================================================================
 * Helper Functions
 * ============================================================================ */

/**
 * Convert VMA flags to page table flags
 */
static uint32_t vma_flags_to_pte(uint32_t vm_flags)
{
    uint32_t pte_flags = 0x01; /* Present */
    
    if (vm_flags & VM_WRITE) {
        pte_flags |= 0x02; /* Read/Write */
    }
    
    /* User-accessible (always set for user VMAs) */
    pte_flags |= 0x04;
    
    /* Note: x86 doesn't have a separate execute bit in 32-bit mode */
    /* We could use NX bit if PAE is enabled, but that's for later */
    
    return pte_flags;
}

/**
 * Check if access is valid for VMA
 */
static int check_access(vma_t *vma, uint32_t error_code)
{
    /* Check read access (always allowed if VMA is readable) */
    if (!(error_code & PF_WRITE)) {
        return (vma->vm_flags & VM_READ) ? 1 : 0;
    }
    
    /* Check write access */
    if (error_code & PF_WRITE) {
        return (vma->vm_flags & VM_WRITE) ? 1 : 0;
    }
    
    return 0;
}

/**
 * Allocate and map a page for the faulting address
 */
static int allocate_page(task_struct_t *task, uint32_t fault_addr, vma_t *vma)
{
    /* Allocate physical page */
    void *page_virt = page_alloc(PAGE_SIZE);
    if (!page_virt) {
        printk("[PAGEFAULT] Failed to allocate physical page\n");
        return -1;
    }
    
    /* Zero the page */
    memset(page_virt, 0, PAGE_SIZE);
    
    /* Get physical address */
    uint32_t page_phys = VIRT_TO_PHYS((uint32_t)page_virt);
    
    /* Page-align fault address */
    uint32_t vaddr = PAGE_ALIGN_DOWN(fault_addr);
    
    /* Convert VMA flags to PTE flags */
    uint32_t pte_flags = vma_flags_to_pte(vma->vm_flags);
    
    /* Map page in user's page directory */
    if (map_page(task->mm.pgdir, vaddr, page_phys, pte_flags) < 0) {
        printk("[PAGEFAULT] Failed to map page\n");
        page_free(page_virt);
        return -1;
    }
    
    return 0;
}

/* ============================================================================
 * Page Fault Handler
 * ============================================================================ */

void page_fault_handler(uint32_t error_code, uint32_t fault_addr)
{
    task_struct_t *task = current;
    
    /* Check if this is a kernel fault */
    if (!(error_code & PF_USER)) {
        printk("[PAGEFAULT] KERNEL fault at 0x%08x (error=0x%x)\n",
               fault_addr, error_code);
        panic("Kernel page fault");
        return;
    }
    
    /* Find VMA containing fault address */
    vma_t *vma = vma_find(task, fault_addr);
    
    if (!vma) {
        printk("[PAGEFAULT] No VMA found for address 0x%08x\n", fault_addr);
        printk("[PAGEFAULT] Dumping VMAs:\n");
        vma_dump(task);
        printk("[PAGEFAULT] SEGMENTATION FAULT - killing process\n");
        /* TODO: Send SIGSEGV signal, for now just panic */
        panic("Segmentation fault");
        return;
    }
    
    /* Check if this is a stack growth fault */
    /* Check if this is a stack growth fault */
    if ((vma->vm_flags & VM_GROWSDOWN) && fault_addr < vma->vm_start) {
        /* Expand stack VMA downward to include fault address */
        uint32_t new_start = PAGE_ALIGN_DOWN(fault_addr);
        
        /* Check stack growth limit (8MB) */
        if (vma->vm_end - new_start > 8 * 1024 * 1024) {
            printk("[PAGEFAULT] Stack overflow - exceeds 8MB limit\n");
            panic("Stack overflow");
            return;
        }
        
        vma->vm_start = new_start;
    }
    
    /* Check if access is valid for this VMA */
    if (!check_access(vma, error_code)) {
        printk("[PAGEFAULT] Invalid access: VMA flags=0x%x, error=0x%x\n",
               vma->vm_flags, error_code);
        panic("Permission denied");
        return;
    }
    
    /* If page is not present, allocate it */
    if (!(error_code & PF_PRESENT)) {
        if (allocate_page(task, fault_addr, vma) < 0) {
            panic("Out of memory");
            return;
        }
    } else {
        /* Protection violation */
        printk("[PAGEFAULT] Protection violation\n");
        panic("Protection violation");
        return;
    }
}

/* ============================================================================
 * Initialization
 * ============================================================================ */

void pagefault_init(void)
{
    printk("[PAGEFAULT] Page fault handler initialized\n");
}