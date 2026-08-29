/* ============================================================================
 * User-space memory access (i386 page-table walk)
 * ============================================================================ */

#include "arch/paging.h"
#include "arch/uaccess.h"
#include "kernel/sched.h"
#include "lib/string.h"
#include <stddef.h>

/* ============================================================================
 * User Pointer Validation
 * ============================================================================ */

/**
 * check_user_range - Verify a user memory range is mapped in the current
 * task's page tables.
 *
 * Walks current->mm.page_tables[] (the same table the running user CR3 is
 * built from).  Every page in [addr, addr+len) must be present; when
 * need_write is set, pages must also be writable.  Returns 1 if valid,
 * 0 otherwise.
 */
static int check_user_range(const void *ptr, size_t len, int need_write)
{
    uint32_t addr = (uint32_t)ptr;
    uint32_t end  = addr + len;

    /* Check for wraparound */
    if (end < addr) {
        return 0;
    }

    /* Must be below kernel space */
    if (addr >= KERNEL_VMA) {
        return 0;
    }

    if (end > KERNEL_VMA) {
        return 0;
    }

    if (len == 0) {
        return 1;
    }

    task_struct_t *task = current;
    if (!task) {
        return 0;
    }

    for (uint32_t a = addr; a < end; a += 0x1000) {
        uint32_t pdi = a >> 22;
        if (pdi >= 768) {
            return 0;   /* above 3GB is kernel space */
        }
        uint32_t *pt = task->mm.arch.page_tables[pdi];
        if (!pt) {
            return 0;   /* no page table for this 4MB region */
        }
        uint32_t pte = pt[(a >> 12) & 0x3FF];
        if (!(pte & 0x1)) {
            return 0;   /* not present */
        }
        if (need_write && !(pte & 0x2)) {
            return 0;   /* read-only */
        }
    }

    return 1;
}

/**
 * Validate a user pointer is within user address space
 * User space: 0x00000000 - 0xBFFFFFFF
 * Kernel space: 0xC0000000 - 0xFFFFFFFF
 */
int valid_user_pointer(const void *ptr, size_t len)
{
    return check_user_range(ptr, len, 0);
}

/**
 * Copy data from userspace to kernel space safely
 */
int copy_from_user(void *kernel_dst, const void *user_src, size_t n)
{
    /* Validate user pointer */
    if (!valid_user_pointer(user_src, n)) {
        return -1;
    }

    /* Copy data */
    memcpy(kernel_dst, user_src, n);
    return 0;
}

/**
 * Copy data from kernel space to userspace safely
 */
int copy_to_user(void *user_dst, const void *kernel_src, size_t n)
{
    /* Validate user pointer AND writability (page-table walk) */
    if (!check_user_range(user_dst, n, 1)) {
        return -1;
    }

    /* Copy data */
    memcpy(user_dst, kernel_src, n);
    return 0;
}
