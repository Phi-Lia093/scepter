/* ============================================================================
 * Page Table Management Utilities (i386 two-level paging)
 * ============================================================================ */

#include "arch/paging.h"
#include "arch/abi.h"
#include "kernel/sched.h"
#include "mm/buddy.h"
#include "lib/printk.h"
#include "lib/string.h"
#include <stdint.h>
#include <stddef.h>

/* Forward declaration for allocation */
extern void *page_alloc_flags(size_t size, uint32_t flags);

/* ============================================================================
 * Page Table Entry Access
 * ============================================================================ */

/**
 * Get the Page Table Entry (PTE) for a virtual address
 * @param virt_addr Virtual address to look up
 * @return Pointer to PTE, or NULL if page table not present
 */
uint32_t* get_pte(uint32_t virt_addr)
{
    uint32_t pde_idx = virt_addr >> 22;           /* Top 10 bits */
    uint32_t pte_idx = (virt_addr >> 12) & 0x3FF; /* Middle 10 bits */
    
    /* Get page directory entry */
    uint32_t pde = boot_page_directory[pde_idx];
    if (!(pde & 0x1)) {
        return NULL;  /* Page directory entry not present */
    }
    
    /* Get page table (convert physical address to virtual) */
    uint32_t* pt = (uint32_t*)((pde & ~0xFFF) + KERNEL_VMA);
    return &pt[pte_idx];
}

/* ============================================================================
 * Page Mapping Operations
 * ============================================================================ */

/**
 * Map a physical page to a virtual page in a specific page directory
 * @param pgdir Page directory to map in (NULL = current/boot)
 * @param virt_addr Virtual address (will be page-aligned)
 * @param phys_addr Physical address (will be page-aligned)
 * @param flags Page flags (Present | Writable | User | etc.)
 * @return 0 on success, -1 on error
 */
int map_page(void *pgdir, uintptr_t virt_addr, uintptr_t phys_addr, uint32_t flags)
{
    /* Use boot page directory if none specified */
    if (!pgdir) {
        pgdir = boot_page_directory;
    }
    uint32_t *pgdir32 = (uint32_t *)pgdir;
    
    /* Page-align addresses */
    virt_addr &= ~0xFFF;
    phys_addr &= ~0xFFF;
    
    /* Get page directory entry */
    uint32_t pde_idx = (uint32_t)virt_addr >> 22;
    uint32_t *pde = &pgdir32[pde_idx];
    
    /* Allocate page table if not present */
    if (!(*pde & 0x1)) {
        /* Allocate physical page for page table */
        void *pt_phys = page_alloc_flags(PAGE_SIZE, MEM_PHY);
        if (!pt_phys) {
            printk("[PGTABLE] ERROR: Cannot allocate page table\n");
            return -1;
        }
        
        /* Set page directory entry with proper flags
         * Must include U/S bit if mapping user pages */
        uint32_t pde_flags = 0x3;  /* Present | Writable */
        if (flags & 0x4) {  /* If PTE has U/S bit */
            pde_flags |= 0x4;  /* Set U/S in PDE too */
        }
        *pde = (uint32_t)pt_phys | pde_flags;
        
        /* Zero the page table (access via direct-mapped region)
         * Page tables themselves are allocated from direct-mapped region */
        uint32_t *pt_virt = (uint32_t *)PHYS_TO_VIRT((uint32_t)pt_phys);
        for (int i = 0; i < 1024; i++) {
            pt_virt[i] = 0;
        }
    }
    
    /* Get page table and set PTE */
    uint32_t *pt = (uint32_t *)((*pde & ~0xFFF) + KERNEL_VMA);
    uint32_t pte_idx = (virt_addr >> 12) & 0x3FF;
    
    pt[pte_idx] = phys_addr | flags;
    
    /* Flush TLB for this address */
    asm volatile("invlpg (%0)" :: "r"(virt_addr) : "memory");
    
    return 0;
}

/**
 * Invalidate (unmap) a single virtual page
 * @param virt_addr Virtual address of page to unmap (will be page-aligned)
 */
void unmap_page(uint32_t virt_addr)
{
    virt_addr &= ~0xFFF;  /* Page-align address */
    
    uint32_t* pte = get_pte(virt_addr);
    if (pte && (*pte & 0x1)) {
        *pte = 0;  /* Mark as not present */
        
        /* Flush TLB entry for this page */
        asm volatile("invlpg (%0)" :: "r"(virt_addr) : "memory");
    }
}

/**
 * Invalidate (unmap) a range of virtual pages
 * @param virt_start Start of virtual address range (inclusive)
 * @param virt_end End of virtual address range (exclusive)
 */
void unmap_range(uint32_t virt_start, uint32_t virt_end)
{
    /* Page-align addresses */
    virt_start &= ~0xFFF;
    virt_end = (virt_end + 0xFFF) & ~0xFFF;
    
    /* Handle wrap-around case (when virt_end is near 0xFFFFFFFF) */
    if (virt_end == 0 || virt_end <= virt_start) {
        /* Don't try to unmap if range is invalid */
        return;
    }
    
    uint32_t pages_unmapped = 0;
    
    for (uint32_t addr = virt_start; addr < virt_end && addr >= virt_start; addr += PAGE_SIZE) {
        unmap_page(addr);
        pages_unmapped++;
    }
    
    printk("[PGTABLE] Unmapped %u pages (0x%08x-0x%08x)\n",
           pages_unmapped, virt_start, virt_end - 1);
}

/**
 * Flush entire TLB (Translation Lookaside Buffer)
 */
void flush_tlb(void)
{
    uint32_t cr3;
    asm volatile("mov %%cr3, %0" : "=r"(cr3));
    asm volatile("mov %0, %%cr3" :: "r"(cr3) : "memory");
}

/* ============================================================================
 * Page Directory Management
 * ============================================================================ */

/**
 * Create a new user page directory with kernel mappings (supervisor-only)
 * User space (0-767): empty, will be filled by exec
 * Kernel space (768-1023): copied from boot pgdir, supervisor-only
 * 
 * @return Physical address of new page directory, or NULL on error
 */
void *create_user_pgdir(void)
{
    /* Allocate page directory (must be page-aligned) */
    uint32_t *pgdir_phys = (uint32_t*)page_alloc_flags(PAGE_SIZE, MEM_PHY);
    if (!pgdir_phys) {
        printk("[PGTABLE] Failed to allocate user page directory\n");
        return NULL;
    }
    
    /* Get virtual address for accessing the page directory */
    uint32_t *pgdir_virt = (uint32_t*)PHYS_TO_VIRT((uint32_t)pgdir_phys);
    
    /* Initialize user space entries (0-767) to 0 */
    for (int i = 0; i < 768; i++) {
        pgdir_virt[i] = 0;
    }
    
    /* Copy kernel space entries (768-1023) from boot page directory */
    /* These are already supervisor-only (U/S bit = 0) */
    for (int i = 768; i < 1024; i++) {
        pgdir_virt[i] = boot_page_directory[i];
    }
    
    printk("[PGTABLE] Created user page directory at phys 0x%08x (kernel mapped as supervisor)\n",
           (uint32_t)pgdir_phys);
    
    return pgdir_phys;
}

/* ============================================================================
 * Page Table Statistics
 * ============================================================================ */

/**
 * Count mapped pages in a virtual address range
 * @param virt_start Start of range
 * @param virt_end End of range (exclusive)
 * @return Number of mapped pages
 */
uint32_t count_mapped_pages(uint32_t virt_start, uint32_t virt_end)
{
    virt_start &= ~0xFFF;
    virt_end = (virt_end + 0xFFF) & ~0xFFF;
    
    uint32_t mapped = 0;
    
    for (uint32_t addr = virt_start; addr < virt_end; addr += PAGE_SIZE) {
        uint32_t* pte = get_pte(addr);
        if (pte && (*pte & 0x1)) {
            mapped++;
        }
    }
    
    return mapped;
}

/* ============================================================================
 * Per-process address space (arch_mm) API
 *
 * Implements the arch_mm_* contract for i386 two-level paging.
 * ============================================================================ */

void arch_mm_init(struct task_struct *task)
{
    /* Zero the whole MMU state, then copy the kernel half of the page
     * directory (entries 768-1023) from the boot page directory. */
    memset(&task->mm.arch, 0, sizeof(task->mm.arch));
    memcpy(&task->mm.arch.pgdir[768], &boot_page_directory[768],
           256 * sizeof(uint32_t));
}

int arch_mm_map_user(struct task_struct *task, uintptr_t vaddr, uintptr_t phys, uint32_t flags)
{
    uint32_t pdi = (uint32_t)vaddr >> 22;
    uint32_t pti = ((uint32_t)vaddr >> 12) & 0x3FF;

    if (pdi >= 768) {
        printk("[MM] ERROR: arch_mm_map_user on kernel address 0x%08x\n",
               (uint32_t)vaddr);
        return -1;
    }

    /* Allocate the page table on demand. */
    if (!task->mm.arch.page_tables[pdi]) {
        uint32_t *pt = (uint32_t *)page_alloc(PAGE_SIZE);
        if (!pt) {
            printk("[MM] arch_mm_map_user: cannot allocate page table\n");
            return -1;
        }
        memset(pt, 0, PAGE_SIZE);
        task->mm.arch.page_tables[pdi] = pt;
        task->mm.arch.pgdir[pdi] = VIRT_TO_PHYS((uint32_t)pt) | 0x7; /* P|RW|U */
    }

    task->mm.arch.page_tables[pdi][pti] = ((uint32_t)phys & ~0xFFF) | flags;
    return 0;
}

uint32_t arch_mm_get_pgd_phys(struct task_struct *task)
{
    return VIRT_TO_PHYS((uint32_t)&task->mm.arch.pgdir[0]);
}

int arch_mm_user_present(struct task_struct *task, uintptr_t vaddr)
{
    uint32_t pdi = (uint32_t)vaddr >> 22;
    uint32_t pti = ((uint32_t)vaddr >> 12) & 0x3FF;

    if (pdi >= 768)
        return 0;
    uint32_t *pt = task->mm.arch.page_tables[pdi];
    if (!pt)
        return 0;
    return (pt[pti] & 0x1) ? 1 : 0;
}

void arch_mm_free_user_pages(struct task_struct *task, uintptr_t start, uintptr_t end)
{
    uint32_t a = (uint32_t)start & ~0xFFF;
    uint32_t e = (uint32_t)end;

    while (a < e) {
        uint32_t pdi = a >> 22;
        uint32_t pti = (a >> 12) & 0x3FF;

        uint32_t *pt = task->mm.arch.page_tables[pdi];
        if (pt && (pt[pti] & 0x1)) {
            uint32_t phys = pt[pti] & ~0xFFF;
            page_free(PHYS_TO_VIRT(phys));
        }
        a += PAGE_SIZE;
    }
}

void arch_mm_free_user_tables(struct task_struct *task)
{
    for (int i = 0; i < 768; i++) {
        if (task->mm.arch.page_tables[i]) {
            page_free(task->mm.arch.page_tables[i]);
            task->mm.arch.page_tables[i] = NULL;
            task->mm.arch.pgdir[i] = 0;
        }
    }
}

int arch_mm_copy_user(struct task_struct *parent, struct task_struct *child)
{
    for (int pdi = 0; pdi < 768; pdi++) {
        uint32_t *parent_pt = parent->mm.arch.page_tables[pdi];
        if (!parent_pt)
            continue;

        for (int pti = 0; pti < 1024; pti++) {
            uint32_t parent_pte = parent_pt[pti];
            if (!(parent_pte & 0x1))   /* not present */
                continue;

            /* Allocate child page table on demand. */
            if (!child->mm.arch.page_tables[pdi]) {
                uint32_t *pt = (uint32_t *)page_alloc(PAGE_SIZE);
                if (!pt)
                    return -1;
                memset(pt, 0, PAGE_SIZE);
                child->mm.arch.page_tables[pdi] = pt;
                child->mm.arch.pgdir[pdi] = VIRT_TO_PHYS((uint32_t)pt) | 0x7;
            }

            /* Copy the page content (eager copy; no COW). */
            void *child_page = page_alloc(PAGE_SIZE);
            if (!child_page)
                return -1;

            memcpy(child_page, PHYS_TO_VIRT(parent_pte & ~0xFFF), PAGE_SIZE);

            uint32_t flags = parent_pte & 0xFFF;   /* copy page flags */
            uint32_t child_phys = VIRT_TO_PHYS((uint32_t)child_page);
            child->mm.arch.page_tables[pdi][pti] = child_phys | flags;
        }
    }
    return 0;
}

void arch_mm_set_user_writable(struct task_struct *task, uintptr_t vaddr, int writable)
{
    uint32_t pdi = (uint32_t)vaddr >> 22;
    uint32_t pti = ((uint32_t)vaddr >> 12) & 0x3FF;

    uint32_t *pt = task->mm.arch.page_tables[pdi];
    if (!pt)
        return;

    if (writable)
        pt[pti] |= 0x2;
    else
        pt[pti] &= ~0x2;

    __asm__ volatile("invlpg (%0)" :: "r"((uint32_t)vaddr) : "memory");
}

void arch_mm_unmap_user_range(struct task_struct *task, uintptr_t start, uintptr_t end)
{
    uint32_t a = (uint32_t)start & ~0xFFF;
    uint32_t e = (uint32_t)end;

    while (a < e) {
        uint32_t pdi = a >> 22;
        uint32_t pti = (a >> 12) & 0x3FF;

        uint32_t *pt = task->mm.arch.page_tables[pdi];
        if (pt && (pt[pti] & 0x1)) {
            pt[pti] = 0;
            __asm__ volatile("invlpg (%0)" :: "r"(a) : "memory");
        }
        a += PAGE_SIZE;
    }
}
