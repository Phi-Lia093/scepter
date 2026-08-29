#ifndef ARCH_ABI_H
#define ARCH_ABI_H

#include <stdint.h>

/* =========================================================================
 * CPU register trap frame + user-space memory layout – arch-neutral ABI.
 *
 * The exact layout of these structures is fixed by the architecture's
 * syscall/interrupt ABI.  Generic kernel code (fork, syscall handling,
 * exec) must only touch them through the accessors below.
 * ========================================================================= */

/* =========================================================================
 * CPU Register State (x86_64)
 *
 * Layout matches what the arch syscall stub (arch/x86_64/isr.s) pushes.
 * ========================================================================= */

typedef struct registers {
    uint64_t rax, rbx, rcx, rdx, rsi, rdi, rbp;
    uint64_t r8, r9, r10, r11, r12, r13, r14, r15;
    /* syscall/iret frame */
    uint64_t rip, cs, rflags, rsp, ss;
} registers_t;

/* =========================================================================
 * Trap-frame accessors used by GENERIC kernel code.
 * ========================================================================= */

#define REGS_RET(r)      ((r)->rax)       /* syscall return value slot  */
#define REGS_IP(r)       ((r)->rip)       /* user instruction pointer   */
#define REGS_USER_SP(r)  ((r)->rsp)       /* user stack pointer         */
#define REGS_FLAGS(r)    ((r)->rflags)    /* flags register             */
#define REGS_CS(r)       ((r)->cs)        /* user code segment          */
#define REGS_SS(r)       ((r)->ss)        /* user stack segment         */

/* =========================================================================
 * Per-process MMU state (arch_mm_t)
 *
 * Embedded in mm_struct_t.  Generic kernel code must NEVER touch these
 * fields directly; it uses the arch_mm_* API declared in arch/paging.h.
 * ========================================================================= */

typedef struct arch_mm {
    /* Kernel-virtual pointer to the task's PML4 page (allocated) and its
     * physical address (the CR3 value for this task). */
    uint64_t *pml4;    /* kernel virtual address of the PML4 page */
    uint64_t  cr3;     /* physical address of the PML4 (CR3)       */
} arch_mm_t;

/* =========================================================================
 * User Space Memory Layout
 *
 * User space is kept in the low 4 GB (x32-style) for now; the user ABI
 * structs (stat/timespec/...) keep their 32-bit layouts so kernel<->libc
 * byte-layout stays compatible.  (LP64 structs are a later stage.)
 * ========================================================================= */

#define USER_TEXT_START  0x08000000U  /* User code starts at 128MB */
#define USER_HEAP_START  0x08100000U  /* Heap starts 1MB after code */
#define USER_STACK_TOP   0xC0000000U  /* Stack grows down from kernel base */
#define USER_STACK_SIZE  0x00100000U  /* 1MB stack */

/* Kernel stack size per task (16 KB). */
#define KERNEL_STACK_SIZE 16384
#define KERNEL_STACK_PAGES (KERNEL_STACK_SIZE / 4096)

#endif /* ARCH_ABI_H */
