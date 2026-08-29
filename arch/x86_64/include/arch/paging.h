#ifndef ARCH_PAGING_H
#define ARCH_PAGING_H

#include <stdint.h>

/* Forward declaration (kernel/sched.h) */
struct task_struct;

/* =========================================================================
 * Paging / address-space API – arch-neutral interface (x86_64 impl).
 * ========================================================================= */

#define PAGE_SIZE  4096

/* Kernel higher-half base (kernel code model). */
#define KERNEL_VMA              0xFFFFFFFF80000000ULL

/* Direct map: the whole of physical RAM is mapped 1:1 at KERNEL_VMA. */
#define KERNEL_DIRECT_MAP_LIMIT 0x1000000000ULL    /* 64 GB cap for now */

/* vmalloc / fixmap live just below the top of the kernel half.  Keep the
 * region modest (1 GB) so its allocator bitmap stays small. */
#define KERNEL_VMALLOC_BASE     0xFFFFFFFFC0000000ULL
#define KERNEL_VMALLOC_END      0xFFFFFFFFFFFFFFFFULL
#define KERNEL_FIXMAP_BASE      0xFFFFFF0000000000ULL

/* Helper macros for address conversion */
#define PHYS_TO_VIRT(phys)  ((void*)((uintptr_t)(phys) + KERNEL_VMA))
#define VIRT_TO_PHYS(virt)  ((uintptr_t)(virt) - KERNEL_VMA)

/* Boot page tables (defined in arch/x86_64/boot.s) */
extern uint64_t boot_pml4[];

/**
 * arch_kernel_pgdir - Virtual address of the boot (kernel) PML4.
 */
uint64_t *arch_kernel_pgdir(void);

/**
 * arch_kernel_pgdir_phys - Physical address (CR3 value) of the boot PML4.
 */
uintptr_t arch_kernel_pgdir_phys(void);

/* =========================================================================
 * Page-table primitives (4-level paging)
 * ========================================================================= */

uint64_t *get_pte(uintptr_t virt_addr);
int map_page(void *pgdir, uintptr_t virt_addr, uintptr_t phys_addr, uint32_t flags);
void unmap_page(uintptr_t virt_addr);
void unmap_range(uintptr_t virt_start, uintptr_t virt_end);
void flush_tlb(void);
uint32_t count_mapped_pages(uintptr_t virt_start, uintptr_t virt_end);
void *create_user_pgdir(void);

/* =========================================================================
 * Per-process address space (arch_mm) API
 * ========================================================================= */

void arch_mm_init(struct task_struct *task);
void arch_mm_free_user_pages(struct task_struct *task, uintptr_t start, uintptr_t end);
void arch_mm_free_user_tables(struct task_struct *task);
int arch_mm_copy_user(struct task_struct *parent, struct task_struct *child);
int arch_mm_map_user(struct task_struct *task, uintptr_t vaddr, uintptr_t phys, uint32_t flags);
uintptr_t arch_mm_get_pgd_phys(struct task_struct *task);
int arch_mm_user_present(struct task_struct *task, uintptr_t vaddr);
void arch_mm_set_user_writable(struct task_struct *task, uintptr_t vaddr, int writable);
void arch_mm_unmap_user_range(struct task_struct *task, uintptr_t start, uintptr_t end);

#endif /* ARCH_PAGING_H */
