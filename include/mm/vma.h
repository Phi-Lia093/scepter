#ifndef VMA_H
#define VMA_H

#include <stdint.h>
#include "lib/list.h"

/* =========================================================================
 * Virtual Memory Area (VMA) - Memory Region Tracking
 * ========================================================================= */

/* VMA types */
#define VMA_CODE     0x01  /* Code segment */
#define VMA_HEAP     0x02  /* Heap (brk) */
#define VMA_STACK    0x03  /* User stack */
#define VMA_MMAP     0x04  /* mmap region */

/* VMA flags (Linux-compatible) */
#define VM_READ      0x01  /* Readable */
#define VM_WRITE     0x02  /* Writable */
#define VM_EXEC      0x04  /* Executable */
#define VM_SHARED    0x08  /* Shared mapping */
#define VM_GROWSDOWN 0x10  /* Stack-like segment */

/* VMA structure */
typedef struct vma {
    list_head_t list;           /* Linked list node */
    uint32_t    vm_start;       /* Start address (page-aligned) */
    uint32_t    vm_end;         /* End address (exclusive, page-aligned) */
    uint32_t    vm_flags;       /* Permissions: VM_READ|VM_WRITE|VM_EXEC */
    uint32_t    vm_type;        /* VMA_HEAP, VMA_STACK, VMA_MMAP, VMA_CODE */
} vma_t;

/* Forward declaration */
struct task_struct;

/* =========================================================================
 * VMA Management Functions
 * ========================================================================= */

/**
 * Create a new VMA
 * @param start Start address (will be page-aligned)
 * @param end End address (will be page-aligned)
 * @param flags VM_READ|VM_WRITE|VM_EXEC
 * @param type VMA_CODE|VMA_HEAP|VMA_STACK|VMA_MMAP
 * @return Pointer to VMA, or NULL on failure
 */
vma_t *vma_create(uint32_t start, uint32_t end, uint32_t flags, uint32_t type);

/**
 * Destroy a VMA and free its memory
 * @param vma VMA to destroy
 */
void vma_destroy(vma_t *vma);

/**
 * Find VMA containing the given address
 * @param task Task structure containing VMA list
 * @param addr Address to search for
 * @return VMA containing addr, or NULL if not found
 */
vma_t *vma_find(struct task_struct *task, uint32_t addr);

/**
 * Insert VMA into task's VMA list (sorted by address)
 * @param task Task structure
 * @param vma VMA to insert
 */
void vma_insert(struct task_struct *task, vma_t *vma);

/**
 * Remove VMA from task's VMA list
 * @param task Task structure
 * @param vma VMA to remove
 */
void vma_remove(struct task_struct *task, vma_t *vma);

/**
 * Find a free region in the mmap area
 * @param task Task structure
 * @param length Size of region needed (will be page-aligned)
 * @param hint Preferred address (0 = kernel chooses)
 * @return Start address of free region, or 0 on failure
 */
uint32_t vma_find_free_region(struct task_struct *task, uint32_t length, uint32_t hint);

/**
 * Check if address range overlaps with any existing VMA
 * @param task Task structure
 * @param start Start address
 * @param end End address
 * @return 1 if overlaps, 0 if free
 */
int vma_overlaps(struct task_struct *task, uint32_t start, uint32_t end);

/**
 * Expand VMA (for stack growth or heap expansion)
 * @param vma VMA to expand
 * @param new_end New end address (must be > current end for normal VMAs)
 * @return 0 on success, -1 on failure
 */
int vma_expand(vma_t *vma, uint32_t new_end);

/**
 * Print all VMAs for debugging
 * @param task Task structure
 */
void vma_dump(struct task_struct *task);

#endif /* VMA_H */