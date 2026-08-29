/* ============================================================================
 * x86_64 4-level page-table management + per-process address space
 * ============================================================================ */

#include "arch/paging.h"
#include "arch/abi.h"
#include "kernel/sched.h"
#include "mm/buddy.h"
#include "lib/printk.h"
#include "lib/string.h"
#include <stdint.h>
#include <stddef.h>

extern void *page_alloc_flags(size_t size, uint32_t flags);

/* ---- 4-level paging constants ---- */
#define PML4_SHIFT 39
#define PDPT_SHIFT 30
#define PD_SHIFT   21
#define PT_SHIFT   12
#define ECOUNT     512
#define PTE_PRESENT  0x001
#define PTE_WRITABLE 0x002
#define PTE_USER     0x004
#define PTE_HUGE     0x080

extern uint64_t boot_pml4[];

static inline uint64_t *pml4_slot(uint64_t *pml4, uintptr_t va)
{ return &pml4[(va >> PML4_SHIFT) & (ECOUNT - 1)]; }
static inline uint64_t *pdpt_slot(uint64_t *pdpt, uintptr_t va)
{ return &pdpt[(va >> PDPT_SHIFT) & (ECOUNT - 1)]; }
static inline uint64_t *pd_slot(uint64_t *pd, uintptr_t va)
{ return &pd[(va >> PD_SHIFT) & (ECOUNT - 1)]; }
static inline uint64_t *pt_slot(uint64_t *pt, uintptr_t va)
{ return &pt[(va >> PT_SHIFT) & (ECOUNT - 1)]; }

/* Kernel-virtual alias of a page-table page given its physical address. */
static inline uint64_t *pt_virt(uint64_t phys)
{ return (uint64_t *)PHYS_TO_VIRT(phys); }

/* ============================================================================
 * Page Table Entry Access
 * ============================================================================ */

uint64_t *get_pte(uintptr_t virt_addr)
{
    uint64_t *pml4 = boot_pml4;

    uint64_t *e = pml4_slot(pml4, virt_addr);
    if (!(*e & PTE_PRESENT)) return NULL;
    uint64_t *pdpt = pt_virt(*e & ~0xFFFULL);
    e = pdpt_slot(pdpt, virt_addr);
    if (!(*e & PTE_PRESENT)) return NULL;
    uint64_t *pd = pt_virt(*e & ~0xFFFULL);
    e = pd_slot(pd, virt_addr);
    if (!(*e & PTE_PRESENT)) return NULL;
    if (*e & PTE_HUGE) return e;          /* 2 MB huge page */
    uint64_t *pt = pt_virt(*e & ~0xFFFULL);
    return pt_slot(pt, virt_addr);
}

/* ============================================================================
 * Page Mapping
 * ============================================================================ */

/* Allocate a fresh zeroed page-table page; returns its physical address. */
static uint64_t alloc_pt_page(void)
{
    void *p = page_alloc_flags(PAGE_SIZE, MEM_PHY);
    if (!p) return 0;
    memset(PHYS_TO_VIRT((uintptr_t)p), 0, PAGE_SIZE);
    return (uint64_t)(uintptr_t)p;
}

int map_page(void *pgdir, uintptr_t virt_addr, uintptr_t phys_addr, uint32_t flags)
{
    uint64_t *pgdir64 = pgdir ? (uint64_t *)pgdir : boot_pml4;
    virt_addr &= ~0xFFFULL;
    phys_addr &= ~0xFFFULL;

    uint64_t *e = pml4_slot(pgdir64, virt_addr);
    if (!(*e & PTE_PRESENT)) {
        uint64_t p = alloc_pt_page();
        if (!p) return -1;
        *e = p | PTE_PRESENT | PTE_WRITABLE | PTE_USER;
    }
    uint64_t *pdpt = pt_virt(*e & ~0xFFFULL);
    e = pdpt_slot(pdpt, virt_addr);
    if (!(*e & PTE_PRESENT)) {
        uint64_t p = alloc_pt_page();
        if (!p) return -1;
        *e = p | PTE_PRESENT | PTE_WRITABLE | PTE_USER;
    }
    uint64_t *pd = pt_virt(*e & ~0xFFFULL);
    e = pd_slot(pd, virt_addr);
    if (!(*e & PTE_PRESENT)) {
        uint64_t p = alloc_pt_page();
        if (!p) return -1;
        *e = p | PTE_PRESENT | PTE_WRITABLE | PTE_USER;
    }
    uint64_t *pt = pt_virt(*e & ~0xFFFULL);
    *pt_slot(pt, virt_addr) = (uint64_t)phys_addr | flags;
    return 0;
}

void unmap_page(uintptr_t virt_addr)
{
    uint64_t *pte = get_pte(virt_addr);
    if (pte && (*pte & PTE_PRESENT)) {
        *pte = 0;
        __asm__ volatile("invlpg (%0)" :: "r"(virt_addr) : "memory");
    }
}

void unmap_range(uintptr_t virt_start, uintptr_t virt_end)
{
    virt_start &= ~0xFFFULL;
    virt_end = (virt_end + 0xFFF) & ~0xFFFULL;

    if (virt_end == 0 || virt_end <= virt_start)
        return;

    for (uintptr_t a = virt_start; a < virt_end; a += PAGE_SIZE)
        unmap_page(a);
}

void flush_tlb(void)
{
    uint64_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    __asm__ volatile("mov %0, %%cr3" :: "r"(cr3) : "memory");
}

uint32_t count_mapped_pages(uintptr_t virt_start, uintptr_t virt_end)
{
    virt_start &= ~0xFFFULL;
    virt_end = (virt_end + 0xFFF) & ~0xFFFULL;

    uint32_t mapped = 0;
    for (uintptr_t a = virt_start; a < virt_end; a += PAGE_SIZE) {
        uint64_t *pte = get_pte(a);
        if (pte && (*pte & PTE_PRESENT)) {
            mapped += (*pte & PTE_HUGE) ? 512 : 1;
            if (*pte & PTE_HUGE) a += (2 * 1024 * 1024) - PAGE_SIZE;
        }
    }
    return mapped;
}

/* ============================================================================
 * Page Directory Management
 * ============================================================================ */

/* ============================================================================
 * Per-process address space (arch_mm) API
 * ============================================================================ */

void arch_mm_init(struct task_struct *task)
{
    uint64_t p = alloc_pt_page();
    if (!p) {
        printk("[MM] arch_mm_init: cannot allocate PML4\n");
        return;
    }
    uint64_t *pml4 = pt_virt(p);
    /* Copy the whole kernel half (PML4[256..511]) - shallow share, so
     * later kernel-half mappings (vmalloc/ioremap) propagate. */
    for (int i = 256; i < 512; i++)
        pml4[i] = boot_pml4[i];

    task->mm.arch.pml4 = pml4;
    task->mm.arch.cr3  = p;
}

int arch_mm_map_user(struct task_struct *task, uintptr_t vaddr, uintptr_t phys, uint32_t flags)
{
    if (vaddr >= 0x400000000000ULL) {      /* sanity: user addresses only */
        printk("[MM] arch_mm_map_user on kernel address 0x%lx\n", (unsigned long)vaddr);
        return -1;
    }
    if (map_page(task->mm.arch.pml4, vaddr, phys, flags) < 0)
        return -1;
    return 0;
}

uintptr_t arch_mm_get_pgd_phys(struct task_struct *task)
{
    return task->mm.arch.cr3;
}

int arch_mm_user_present(struct task_struct *task, uintptr_t vaddr)
{
    uint64_t *pml4 = task->mm.arch.pml4;
    if (!pml4) return 0;

    uint64_t *e = pml4_slot(pml4, vaddr);
    if (!(*e & PTE_PRESENT)) return 0;
    uint64_t *pdpt = pt_virt(*e & ~0xFFFULL);
    e = pdpt_slot(pdpt, vaddr);
    if (!(*e & PTE_PRESENT)) return 0;
    uint64_t *pd = pt_virt(*e & ~0xFFFULL);
    e = pd_slot(pd, vaddr);
    if (!(*e & PTE_PRESENT)) return 0;
    if (*e & PTE_HUGE) return 1;
    uint64_t *pt = pt_virt(*e & ~0xFFFULL);
    return (*pt_slot(pt, vaddr) & PTE_PRESENT) ? 1 : 0;
}

void arch_mm_free_user_pages(struct task_struct *task, uintptr_t start, uintptr_t end)
{
    uintptr_t a = start & ~0xFFFULL;
    uintptr_t e = end;

    while (a < e) {
        uint64_t *pml4 = task->mm.arch.pml4;
        if (pml4) {
            uint64_t *pde = pml4_slot(pml4, a);
            if (*pde & PTE_PRESENT) {
                uint64_t *pdpt = pt_virt(*pde & ~0xFFFULL);
                uint64_t *pdpte = pdpt_slot(pdpt, a);
                if (*pdpte & PTE_PRESENT) {
                    uint64_t *pd = pt_virt(*pdpte & ~0xFFFULL);
                    uint64_t *pde2 = pd_slot(pd, a);
                    if (*pde2 & PTE_PRESENT) {
                        if (*pde2 & PTE_HUGE) {
                            page_free(PHYS_TO_VIRT((uintptr_t)(*pde2 & ~0x1FFFFFULL)));
                        } else {
                            uint64_t *pt = pt_virt(*pde2 & ~0xFFFULL);
                            uint64_t pte = *pt_slot(pt, a);
                            if (pte & PTE_PRESENT)
                                page_free(PHYS_TO_VIRT((uintptr_t)(pte & ~0xFFFULL)));
                        }
                    }
                }
            }
        }
        a += PAGE_SIZE;
    }
}

void arch_mm_free_user_tables(struct task_struct *task)
{
    uint64_t *pml4 = task->mm.arch.pml4;
    if (!pml4)
        return;

    /* Free user page tables (PML4[0..255] subtrees); keep the shared
     * kernel half (256..511). */
    for (int i = 0; i < 256; i++) {
        uint64_t pml4e = pml4[i];
        if (!(pml4e & PTE_PRESENT)) continue;
        uint64_t *pdpt = pt_virt(pml4e & ~0xFFFULL);
        for (int j = 0; j < 512; j++) {
            uint64_t pdpte = pdpt[j];
            if (!(pdpte & PTE_PRESENT)) continue;
            uint64_t *pd = pt_virt(pdpte & ~0xFFFULL);
            for (int k = 0; k < 512; k++) {
                uint64_t pde = pd[k];
                if (!(pde & PTE_PRESENT) || (pde & PTE_HUGE)) continue;
                page_free(pt_virt(pde & ~0xFFFULL));
            }
            page_free(pd);
        }
        page_free(pdpt);
    }
    page_free(pml4);

    task->mm.arch.pml4 = NULL;
    task->mm.arch.cr3  = 0;
}

void *create_user_pgdir(void)
{
    uint64_t p = alloc_pt_page();
    if (!p) {
        printk("[PGTABLE] Failed to allocate user PML4\n");
        return NULL;
    }
    uint64_t *pml4 = pt_virt(p);
    for (int i = 256; i < 512; i++)
        pml4[i] = boot_pml4[i];
    return (void *)(uintptr_t)p;             /* physical address */
}

/* ============================================================================
 * Per-process address space (arch_mm) API
 * ============================================================================ */
int arch_mm_copy_user(struct task_struct *parent, struct task_struct *child)
{
    uint64_t *ppml4 = parent->mm.arch.pml4;
    uint64_t *cpml4 = child->mm.arch.pml4;
    if (!ppml4 || !cpml4)
        return 0;

    for (int i = 0; i < 256; i++) {
        uint64_t pe = ppml4[i];
        if (!(pe & PTE_PRESENT)) continue;
        uint64_t *ppdpt = pt_virt(pe & ~0xFFFULL);
        for (int j = 0; j < 512; j++) {
            uint64_t ppe = ppdpt[j];
            if (!(ppe & PTE_PRESENT)) continue;
            uint64_t *ppd = pt_virt(ppe & ~0xFFFULL);
            for (int k = 0; k < 512; k++) {
                uint64_t pde = ppd[k];
                if (!(pde & PTE_PRESENT)) continue;

                /* ensure child has PML4/PDPT/PD levels */
                if (!(cpml4[i] & PTE_PRESENT)) {
                    uint64_t p = alloc_pt_page();
                    if (!p) return -1;
                    cpml4[i] = p | PTE_PRESENT | PTE_WRITABLE | PTE_USER;
                }
                uint64_t *cpdpt = pt_virt(cpml4[i] & ~0xFFFULL);
                if (!(cpdpt[j] & PTE_PRESENT)) {
                    uint64_t p = alloc_pt_page();
                    if (!p) return -1;
                    cpdpt[j] = p | PTE_PRESENT | PTE_WRITABLE | PTE_USER;
                }
                uint64_t *cpd = pt_virt(cpdpt[j] & ~0xFFFULL);
                if (!(cpd[k] & PTE_PRESENT)) {
                    uint64_t p = alloc_pt_page();
                    if (!p) return -1;
                    cpd[k] = p | PTE_PRESENT | PTE_WRITABLE | PTE_USER;
                }

                if (pde & PTE_HUGE) {
                    /* copy 2 MB of RAM */
                    void *src = PHYS_TO_VIRT((uintptr_t)(pde & ~0x1FFFFFULL));
                    void *dst = page_alloc(2 * 1024 * 1024);
                    if (!dst) return -1;
                    memcpy(dst, src, 2 * 1024 * 1024);
                    cpd[k] = VIRT_TO_PHYS((uintptr_t)dst) |
                             (uint64_t)(pde & 0x1FFFFFULL) | PTE_HUGE;
                } else {
                    uint64_t *ppt = pt_virt(pde & ~0xFFFULL);
                    uint64_t *cpt = pt_virt(cpd[k] & ~0xFFFULL);
                    for (int m = 0; m < 512; m++) {
                        uint64_t pte = ppt[m];
                        if (!(pte & PTE_PRESENT)) continue;
                        void *src = PHYS_TO_VIRT((uintptr_t)(pte & ~0xFFFULL));
                        void *dst = page_alloc(PAGE_SIZE);
                        if (!dst) return -1;
                        memcpy(dst, src, PAGE_SIZE);
                        cpt[m] = VIRT_TO_PHYS((uintptr_t)dst) |
                                 (uint64_t)(pte & 0xFFFULL);
                    }
                }
            }
        }
    }
    return 0;
}

void arch_mm_set_user_writable(struct task_struct *task, uintptr_t vaddr, int writable)
{
    uint64_t *pml4 = task->mm.arch.pml4;
    if (!pml4) return;

    uint64_t *e = pml4_slot(pml4, vaddr);
    if (!(*e & PTE_PRESENT)) return;
    uint64_t *pdpt = pt_virt(*e & ~0xFFFULL);
    e = pdpt_slot(pdpt, vaddr);
    if (!(*e & PTE_PRESENT)) return;
    uint64_t *pd = pt_virt(*e & ~0xFFFULL);
    e = pd_slot(pd, vaddr);
    if (!(*e & PTE_PRESENT)) return;
    if (*e & PTE_HUGE) return;
    uint64_t *pt = pt_virt(*e & ~0xFFFULL);
    uint64_t *pte = pt_slot(pt, vaddr);
    if (writable)
        *pte |= PTE_WRITABLE;
    else
        *pte &= ~PTE_WRITABLE;
    __asm__ volatile("invlpg (%0)" :: "r"(vaddr) : "memory");
}

void arch_mm_unmap_user_range(struct task_struct *task, uintptr_t start, uintptr_t end)
{
    uintptr_t a = start & ~0xFFFULL;
    uintptr_t e = end;

    while (a < e) {
        uint64_t *pml4 = task->mm.arch.pml4;
        if (pml4) {
            uint64_t *pde = pml4_slot(pml4, a);
            if (*pde & PTE_PRESENT) {
                uint64_t *pdpt = pt_virt(*pde & ~0xFFFULL);
                uint64_t *pdpte = pdpt_slot(pdpt, a);
                if (*pdpte & PTE_PRESENT) {
                    uint64_t *pd = pt_virt(*pdpte & ~0xFFFULL);
                    uint64_t *pde2 = pd_slot(pd, a);
                    if (*pde2 & PTE_PRESENT && !(*pde2 & PTE_HUGE)) {
                        uint64_t *pt = pt_virt(*pde2 & ~0xFFFULL);
                        uint64_t *pte = pt_slot(pt, a);
                        if (*pte & PTE_PRESENT) {
                            *pte = 0;
                            __asm__ volatile("invlpg (%0)" :: "r"(a) : "memory");
                        }
                    }
                }
            }
        }
        a += PAGE_SIZE;
    }
}
