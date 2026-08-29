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

/* Save the interrupt flag and disable interrupts.  Unlike cli()/sti(),
 * this restores the *previous* IF state, so calling it while interrupts
 * are already off does NOT enable them early. */
static inline unsigned long irq_save(void)
{
    unsigned long flags;
    __asm__ volatile ("pushfl; popl %0; cli" : "=g"(flags) : : "memory");
    return flags;
}

/* Restore the interrupt flag saved by irq_save(). */
static inline void irq_restore(unsigned long flags)
{
    __asm__ volatile ("pushl %0; popfl" : : "g"(flags) : "memory");
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
 * arch_cpu_init - Initialize the CPU (GDT + TSS + IDT + exception gates).
 * Called once very early in kernel_main(), before anything else.
 */
void arch_cpu_init(void);

/**
 * arch_set_kernel_stack - Set the ring-0 stack pointer used for
 * user -> kernel privilege transitions (TSS.esp0 on i386).
 * @param esp0  Top of the kernel stack
 */
void arch_set_kernel_stack(uint32_t esp0);

/* =========================================================================
 * Context switching (implemented in arch/<arch>/context.s)
 * ========================================================================= */

/**
 * Low-level context switch.
 * @param old_esp Pointer to save old ESP
 * @param new_esp New ESP to load
 * @param new_cr3 New CR3 (page directory physical address)
 */
void switch_to(uint32_t *old_esp, uint32_t new_esp, uint32_t new_cr3);

/**
 * enter_userspace - Switch from kernel mode to user mode.  Never returns.
 * @param cr3      User page directory physical address
 * @param entry    User entry point
 * @param user_esp Initial user stack pointer
 */
void enter_userspace(uint32_t cr3, uint32_t entry, uint32_t user_esp);

/**
 * Trampoline executed (via ret from switch_to) when a task is scheduled
 * for the FIRST time.  The kernel stack already carries a ring-3 IRET
 * frame; this stub loads user segments and IRETs to user mode.
 */
void first_entry_trampoline(void);

/**
 * arch_setup_first_stack - Build the initial kernel stack frame for a task
 * being scheduled for the first time: the switch_to() popa/popfl frame +
 * the return address + the ring-3 IRET frame (so first_entry_trampoline
 * can IRET into user mode).
 *
 * @param task       Task whose kernel_esp is set; must already point at
 *                   the top of the task's kernel stack
 * @param user_entry User entry point (fresh start only; ignored for fork)
 * @param user_esp   Initial user stack pointer (fresh start only)
 * @param regs       Parent's trap frame to clone (fork), or NULL for a
 *                   fresh start
 */
void arch_setup_first_stack(struct task_struct *task, uintptr_t user_entry,
                            uintptr_t user_esp, const registers_t *regs);

#endif /* ARCH_CPU_H */
