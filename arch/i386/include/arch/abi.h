#ifndef ARCH_ABI_H
#define ARCH_ABI_H

#include <stdint.h>

/* =========================================================================
 * CPU register trap frame + user-space memory layout – arch-neutral ABI.
 *
 * The exact layout of these structures is fixed by the architecture's
 * syscall/interrupt ABI.  Generic kernel code (fork, syscall handling,
 * exec) must only touch them through these declarations.
 * ========================================================================= */

/* =========================================================================
 * CPU Register State (for fork context preservation)
 *
 * Layout matches what the arch syscall stub pushes (see arch/i386/isr.s).
 * ========================================================================= */

typedef struct registers {
    /* Pushed by pusha */
    uint32_t edi, esi, ebp, esp_dummy, ebx, edx, ecx, eax;

    /* Segment selectors */
    uint32_t gs, fs, es, ds;

    /* Saved CR3 */
    uint32_t cr3;

    /* IRET frame (pushed by CPU) */
    uint32_t eip, cs, eflags, user_esp, ss;
} registers_t;

/* =========================================================================
 * User Space Memory Layout
 * ========================================================================= */

#define USER_TEXT_START  0x08000000U  /* User code starts at 128MB */
#define USER_HEAP_START  0x08100000U  /* Heap starts 1MB after code */
#define USER_STACK_TOP   0xC0000000U  /* Stack grows down from kernel base */
#define USER_STACK_SIZE  0x00100000U  /* 1MB stack */

/*
 * Kernel stack size per task.
 *
 * 16 KB (4 pages) instead of 8 KB: deep syscall paths can overflow an 8 KB
 * stack. In particular the minix3 read path keeps a 4 KB block_buf[] on the
 * stack under a deep call chain (int 0x80 -> sys_read -> fs_read ->
 * minix3_vfs_read -> minix3_read_file -> bread -> ide PIO), which pushed
 * the stack past the 8 KB boundary and clobbered an adjacent page table.
 */
#define KERNEL_STACK_SIZE 16384
#define KERNEL_STACK_PAGES (KERNEL_STACK_SIZE / 4096)

#endif /* ARCH_ABI_H */
