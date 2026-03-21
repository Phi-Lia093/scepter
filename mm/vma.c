/* ============================================================================
 * Virtual Memory Area (VMA) Management
 * ============================================================================ */

#include "mm/vma.h"
#include "mm/slab.h"
#include "mm/mm.h"
#include "kernel/sched.h"
#include "lib/printk.h"
#include "lib/string.h"

#define PAGE_SIZE 4096
#define PAGE_ALIGN(addr) (((addr) + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1))
#define PAGE_ALIGN_DOWN(addr) ((addr) & ~(PAGE_SIZE - 1))

/* ============================================================================
 * VMA Creation and Destruction
 * ============================================================================ */

vma_t *vma_create(uint32_t start, uint32_t end, uint32_t flags, uint32_t type)
{
    /* Allocate VMA structure */
    vma_t *vma = (vma_t *)kalloc(sizeof(vma_t));
    if (!vma) {
        printk("[VMA] Failed to allocate VMA structure\n");
        return NULL;
    }
    
    /* Page-align addresses */
    vma->vm_start = PAGE_ALIGN_DOWN(start);
    vma->vm_end = PAGE_ALIGN(end);
    vma->vm_flags = flags;
    vma->vm_type = type;
    
    /* Initialize list node */
    INIT_LIST_HEAD(&vma->list);
    
    return vma;
}

void vma_destroy(vma_t *vma)
{
    if (!vma) return;
    
    /* Remove from list if still linked */
    if (!list_empty(&vma->list)) {
        list_del(&vma->list);
    }
    
    /* Free VMA structure */
    kfree(vma);
}

/* ============================================================================
 * VMA Search and Manipulation
 * ============================================================================ */

vma_t *vma_find(task_struct_t *task, uint32_t addr)
{
    if (!task) return NULL;
    
    list_head_t *pos;
    list_for_each(pos, &task->mm.vma_list) {
        vma_t *vma = list_entry(pos, vma_t, list);
        
        /* For stack VMAs with VM_GROWSDOWN, check if address is below start
         * (within reasonable guard region) */
        if (vma->vm_flags & VM_GROWSDOWN) {
            /* Allow stack to grow down up to 8MB */
            uint32_t stack_limit = vma->vm_start > (8 * 1024 * 1024) ?
                                   vma->vm_start - (8 * 1024 * 1024) : 0;
            if (addr >= stack_limit && addr < vma->vm_end) {
                return vma;
            }
        } else {
            /* Normal VMA: check if address is within range */
            if (addr >= vma->vm_start && addr < vma->vm_end) {
                return vma;
            }
        }
    }
    
    return NULL;
}

void vma_insert(task_struct_t *task, vma_t *vma)
{
    if (!task || !vma) return;
    
    /* Insert VMA sorted by start address */
    list_head_t *pos;
    list_for_each(pos, &task->mm.vma_list) {
        vma_t *existing = list_entry(pos, vma_t, list);
        if (vma->vm_start < existing->vm_start) {
            list_add_tail(&vma->list, pos);
            return;
        }
    }
    
    /* Add at end if no earlier position found */
    list_add_tail(&vma->list, &task->mm.vma_list);
}

void vma_remove(task_struct_t *task, vma_t *vma)
{
    if (!task || !vma) return;
    
    list_del(&vma->list);
}

int vma_overlaps(task_struct_t *task, uint32_t start, uint32_t end)
{
    if (!task) return 0;
    
    list_head_t *pos;
    list_for_each(pos, &task->mm.vma_list) {
        vma_t *vma = list_entry(pos, vma_t, list);
        
        /* Check for overlap: [start, end) overlaps [vma_start, vma_end) */
        if (start < vma->vm_end && end > vma->vm_start) {
            return 1;
        }
    }
    
    return 0;
}

int vma_expand(vma_t *vma, uint32_t new_end)
{
    if (!vma) return -1;
    
    /* Page-align new end */
    new_end = PAGE_ALIGN(new_end);
    
    /* For normal VMAs, new_end must be >= current end */
    if (!(vma->vm_flags & VM_GROWSDOWN)) {
        if (new_end < vma->vm_end) {
            return -1;
        }
        vma->vm_end = new_end;
    } else {
        /* For stack VMAs (VM_GROWSDOWN), expand downward */
        if (new_end > vma->vm_start) {
            return -1;
        }
        vma->vm_start = new_end;
    }
    
    return 0;
}

/* ============================================================================
 * Free Region Finding (for mmap)
 * ============================================================================ */

uint32_t vma_find_free_region(task_struct_t *task, uint32_t length, uint32_t hint)
{
    if (!task) return 0;
    
    /* Page-align length */
    length = PAGE_ALIGN(length);
    
    /* Start search at mmap_base or hint */
    uint32_t search_start = hint ? PAGE_ALIGN(hint) : task->mm.mmap_base;
    uint32_t search_end = task->mm.mmap_end;
    
    /* If hint provided, validate it's in mmap region */
    if (hint && (hint < task->mm.mmap_base || hint >= task->mm.mmap_end)) {
        search_start = task->mm.mmap_base;
    }
    
    /* Simple linear search for free region */
    uint32_t candidate = search_start;
    
    while (candidate + length <= search_end) {
        /* Check if this region overlaps with any existing VMA */
        if (!vma_overlaps(task, candidate, candidate + length)) {
            return candidate;
        }
        
        /* Move to next page */
        candidate += PAGE_SIZE;
    }
    
    return 0; /* No free region found */
}

/* ============================================================================
 * Debugging
 * ============================================================================ */

void vma_dump(task_struct_t *task)
{
    if (!task) return;
    
    printk("[VMA] Process PID %u VMAs:\n", task->pid);
    
    list_head_t *pos;
    int count = 0;
    list_for_each(pos, &task->mm.vma_list) {
        vma_t *vma = list_entry(pos, vma_t, list);
        
        const char *type_str;
        switch (vma->vm_type) {
            case VMA_CODE:  type_str = "CODE "; break;
            case VMA_HEAP:  type_str = "HEAP "; break;
            case VMA_STACK: type_str = "STACK"; break;
            case VMA_MMAP:  type_str = "MMAP "; break;
            default:        type_str = "?????"; break;
        }
        
        printk("  [%d] 0x%08x-0x%08x %c%c%c%c %s\n",
               count++,
               vma->vm_start,
               vma->vm_end,
               (vma->vm_flags & VM_READ)      ? 'r' : '-',
               (vma->vm_flags & VM_WRITE)     ? 'w' : '-',
               (vma->vm_flags & VM_EXEC)      ? 'x' : '-',
               (vma->vm_flags & VM_GROWSDOWN) ? 'd' : '-',
               type_str);
    }
    
    if (count == 0) {
        printk("  (no VMAs)\n");
    }
}