/* ============================================================================
 * Page Fault Handler - Demand Paging Implementation
 * ============================================================================ */

#include "mm/pagefault.h"
#include "mm/vma.h"
#include "arch/paging.h"
#include "mm/buddy.h"
#include "mm/mm.h"
#include "kernel/sched.h"
#include "kernel/process.h"
#include "kernel/signal.h"
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
    uint32_t vaddr = PAGE_ALIGN_DOWN(fault_addr);

    /* Device memory mapping (VM_IO, e.g. /dev/fb0): map the device's
     * physical page directly — no RAM allocation, no file read. */
    if (vma->vm_flags & VM_IO) {
        uint32_t file_off = vma->vm_file_off + (vaddr - vma->vm_start);
        uint32_t dev_phys = vma->vm_phys_base + file_off;
        uint32_t pte_flags = vma_flags_to_pte(vma->vm_flags);

        if (arch_mm_map_user(task, vaddr, dev_phys, pte_flags) < 0) {
            printk("[PAGEFAULT] Failed to map device page\n");
            return -1;
        }
        return 0;
    }

    /* Allocate physical page */
    void *page_virt = page_alloc(PAGE_SIZE);
    if (!page_virt) {
        printk("[PAGEFAULT] Failed to allocate physical page\n");
        return -1;
    }
    
    /* Zero the page */
    memset(page_virt, 0, PAGE_SIZE);

    /* File-backed mapping: load the page content from the file.
     * The fd belongs to the faulting process (vma->vm_fd was captured
     * at mmap() time and its refcount keeps the open_file alive). */
    if (vma->vm_fd >= 0 && vma->vm_type == VMA_MMAP) {
        uint32_t file_off = vma->vm_file_off + (vaddr - vma->vm_start);
        extern int fs_pread(int fd, void *buf, size_t count, uint32_t offset);
        fs_pread(vma->vm_fd, page_virt, PAGE_SIZE, file_off);
    }
    
    /* Get physical address */
    uintptr_t page_phys = VIRT_TO_PHYS((uintptr_t)page_virt);
    
    /* Convert VMA flags to PTE flags */
    uint32_t pte_flags = vma_flags_to_pte(vma->vm_flags);
    
    /* Map page in the task's page directory (also keeps the arch_mm
     * page-table cache in sync, which check_user_range()/copy_to_user()
     * walk for user-range checks). */
    if (arch_mm_map_user(task, vaddr, page_phys, pte_flags) < 0) {
        printk("[PAGEFAULT] Failed to map page\n");
        page_free(page_virt);
        return -1;
    }
    
    return 0;
}

/* ============================================================================
 * Page Fault Handler
 * ============================================================================ */

/**
 * kill_current - Terminate the current task because of a user-space fault.
 * Never returns.  This is how a userland bug (bad pointer, stack overflow,
 * write to read-only, ...) is contained: the process dies with a SIGSEGV
 * rather than panicking the whole kernel.
 */
static void kill_current(uint32_t fault_addr, uint32_t error_code)
{
    task_struct_t *task = current;
    printk("[SIG] pid %d (%s): SIGSEGV at 0x%08x (error=0x%x)\n",
           task ? task->pid : 0, task ? task->name : "?", fault_addr, error_code);
    do_exit(128 + SIGSEGV);
}

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
        vma_dump(task);
        kill_current(fault_addr, error_code);
        return;
    }
    
    /* Check if this is a stack growth fault */
    if ((vma->vm_flags & VM_GROWSDOWN) && fault_addr < vma->vm_start) {
        /* Expand stack VMA downward to include fault address */
        uint32_t new_start = PAGE_ALIGN_DOWN(fault_addr);
        
        /* Check stack growth limit (8MB) */
        if (vma->vm_end - new_start > 8 * 1024 * 1024) {
            printk("[PAGEFAULT] Stack overflow - exceeds 8MB limit\n");
            kill_current(fault_addr, error_code);
            return;
        }
        
        vma->vm_start = new_start;
    }
    
    /* Check if access is valid for this VMA */
    if (!check_access(vma, error_code)) {
        printk("[PAGEFAULT] Invalid access: VMA flags=0x%x, error=0x%x\n",
               vma->vm_flags, error_code);
        kill_current(fault_addr, error_code);
        return;
    }
    
    /* If page is not present, allocate it */
    if (!(error_code & PF_PRESENT)) {
        if (allocate_page(task, fault_addr, vma) < 0) {
            printk("[PAGEFAULT] Out of memory\n");
            kill_current(fault_addr, error_code);
            return;
        }
    } else {
        /* Protection violation */
        printk("[PAGEFAULT] Protection violation\n");
        kill_current(fault_addr, error_code);
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