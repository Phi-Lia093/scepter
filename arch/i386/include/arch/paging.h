#ifndef ARCH_PAGING_H
#define ARCH_PAGING_H

#include <stdint.h>

/* Forward declaration (kernel/sched.h) */
struct task_struct;

/* =========================================================================
 * Paging / address-space API – arch-neutral interface.
 *
 * Defines the kernel address-space layout, the phys<->virt translation
 * helpers, and the page-table manipulation primitives.  The active arch
 * implements these (arch/i386/paging.c) and may use any page-table format
 * it likes underneath.
 * ========================================================================= */

/* Page size: 4 KB */
#define PAGE_SIZE  4096

/* Kernel address-space layout (Linux-style, higher-half) */
#define KERNEL_VMA              0xC0000000U  /* Kernel virtual base */
#define KERNEL_DIRECT_MAP_LIMIT 0x38000000U  /* 896 MB physical limit */
#define KERNEL_VMALLOC_BASE     0xF8000000U  /* vmalloc region start (reserved) */
#define KERNEL_VMALLOC_END      0xFFBFFFFFU  /* vmalloc region end (128 MB) */
#define KERNEL_FIXMAP_BASE      0xFFC00000U  /* Fixed mappings (4 MB, future) */

/* Helper macros for address conversion */
#define PHYS_TO_VIRT(phys)  ((void*)((uint32_t)(phys) + KERNEL_VMA))
#define VIRT_TO_PHYS(virt)  ((uint32_t)(virt) - KERNEL_VMA)

/* Boot page directory / tables (defined in arch/i386/boot.s) */
extern uint32_t boot_page_directory[];
extern uint32_t boot_page_tables[];

/**
 * arch_kernel_pgdir - Virtual address of the boot (kernel) page directory.
 */
uint32_t *arch_kernel_pgdir(void);

/**
 * arch_kernel_pgdir_phys - Physical address (CR3 value) of the boot page
 * directory.  Used as the kernel task's CR3 and to switch to kernel page
 * tables while tearing down a user address space.
 */
uint32_t arch_kernel_pgdir_phys(void);

/* =========================================================================
 * Page Table Entry Access
 * ========================================================================= */

/**
 * Get the Page Table Entry (PTE) for a virtual address.
 * @return Pointer to PTE, or NULL if page table not present
 */
uint32_t* get_pte(uintptr_t virt_addr);

/* =========================================================================
 * Page Mapping Operations
 * ========================================================================= */

/**
 * Map a physical page to a virtual page in a specific page directory.
 * @param pgdir Page directory to map in (NULL = current/boot)
 * @return 0 on success, -1 on error
 */
int map_page(void *pgdir, uintptr_t virt_addr, uintptr_t phys_addr, uint32_t flags);

/**
 * Invalidate (unmap) a single virtual page.
 */
void unmap_page(uintptr_t virt_addr);

/**
 * Invalidate (unmap) a range of virtual pages.
 */
void unmap_range(uintptr_t virt_start, uintptr_t virt_end);

/**
 * Flush the entire TLB.
 */
void flush_tlb(void);

/**
 * Count mapped pages in a virtual address range.
 */
uint32_t count_mapped_pages(uintptr_t virt_start, uintptr_t virt_end);

/* =========================================================================
 * Page Directory Management
 * ========================================================================= */

/**
 * Create a new user page directory with kernel mappings (supervisor-only).
 * @return Physical address of new page directory, or NULL on error
 */
void *create_user_pgdir(void);

/* =========================================================================
 * Per-process address space (arch_mm) API
 *
 * Every task embeds an arch_mm_t (mm_struct_t.arch).  Generic kernel code
 * (sched/process/spawn/pagefault/vma/syscall) manipulates a task's user
 * mappings only through these functions; the arch decides the page-table
 * format (arch/i386: two-level paging).
 * ========================================================================= */

/** Initialize the task's MMU state (zero user half, map kernel half). */
void arch_mm_init(struct task_struct *task);

/** Free all physical pages mapped in [start, end) of the task's user space. */
void arch_mm_free_user_pages(struct task_struct *task, uintptr_t start, uintptr_t end);

/** Free all user page tables and clear the user half of the page directory. */
void arch_mm_free_user_tables(struct task_struct *task);

/**
 * Duplicate the parent's user address space into the child (eager copy:
 * every present user page is copied).  Used by fork().
 * @return 0 on success, -1 on out-of-memory
 */
int arch_mm_copy_user(struct task_struct *parent, struct task_struct *child);

/** Map one user page in the task's address space. */
int arch_mm_map_user(struct task_struct *task, uintptr_t vaddr, uintptr_t phys, uint32_t flags);

/** Physical address (CR3 value) of the task's page directory. */
uint32_t arch_mm_get_pgd_phys(struct task_struct *task);

/** Is the user page at vaddr present in the task's page tables? */
int arch_mm_user_present(struct task_struct *task, uintptr_t vaddr);

/** Set/clear write permission on one already-present user page (mprotect). */
void arch_mm_set_user_writable(struct task_struct *task, uintptr_t vaddr, int writable);

/** Clear (unmap) user PTEs in [start, end) and invalidate the TLB. */
void arch_mm_unmap_user_range(struct task_struct *task, uintptr_t start, uintptr_t end);

#endif /* ARCH_PAGING_H */
