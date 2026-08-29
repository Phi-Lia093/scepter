#ifndef ARCH_CPU_H
#define ARCH_CPU_H

#include <stdint.h>
#include "arch/abi.h"

/* Forward declaration (kernel/sched.h) */
struct task_struct;

/* =========================================================================
 * CPU control + context switching – arch-neutral API provided by the
 * active arch.  Generic kernel code includes <arch/cpu.h>.
 * ========================================================================= */

/* Disable / enable hardware interrupts */
static inline void cli(void)
{
    __asm__ volatile ("cli");
}

static inline void sti(void)
{
    __asm__ volatile ("sti");
}

/* Save the interrupt flag and disable interrupts. */
static inline unsigned long irq_save(void)
{
    unsigned long flags;
    __asm__ volatile ("pushfq; popq %0; cli" : "=g"(flags) : : "memory");
    return flags;
}

/* Restore the interrupt flag saved by irq_save(). */
static inline void irq_restore(unsigned long flags)
{
    __asm__ volatile ("pushq %0; popfq" : : "g"(flags) : "memory");
}

/* Halt the CPU until the next interrupt */
static inline void hlt(void)
{
    __asm__ volatile ("hlt");
}

static inline void magic_break(void)
{
    __asm__ volatile ("xchgw %bx, %bx");
}

/* =========================================================================
 * CPU / MMU bootstrap
 * ========================================================================= */

/**
 * arch_cpu_init - Initialize the CPU (GDT + TSS + IDT + exception gates
 * + syscall MSRs).  Called once very early in kernel_main().
 */
void arch_cpu_init(void);

/**
 * arch_set_kernel_stack - Set the ring-0 stack pointer used for
 * user -> kernel privilege transitions (TSS.rsp0).
 */
void arch_set_kernel_stack(uintptr_t esp0);

/* =========================================================================
 * Context switching (implemented in arch/x86_64/context.s)
 * ========================================================================= */

void switch_to(uintptr_t *old_rsp, uintptr_t new_rsp, uintptr_t new_cr3);
void enter_userspace(uintptr_t cr3, uintptr_t entry, uintptr_t user_rsp);
void first_entry_trampoline(void);

/**
 * arch_setup_first_stack - Build the initial kernel stack frame for a task
 * being scheduled for the first time (switch_to frame + ring-3 iretq
 * frame).  See arch/x86_64/context.s and stackframe.c.
 */
void arch_setup_first_stack(struct task_struct *task, uintptr_t user_entry,
                            uintptr_t user_esp, const registers_t *regs);

#endif /* ARCH_CPU_H */
