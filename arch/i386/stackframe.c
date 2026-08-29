/* ============================================================================
 * Task context setup (i386)
 *
 * Builds the initial kernel stack frame for a task that is being scheduled
 * for the first time, so that switch_to() -> first_entry_trampoline() ->
 * iret lands in user mode.  Layout (see arch/i386/context.s):
 *
 *   ESP+0   EDI        \__ popa frame (switch_to pops first)
 *   ESP+4   ESI         |
 *   ESP+8   EBP         |
 *   ESP+12  dummy       |
 *   ESP+16  EBX         |
 *   ESP+20  EDX         |
 *   ESP+24  ECX         |
 *   ESP+28  EAX         /
 *   ESP+32  EFLAGS     (popfl: IF=0 so the iret enables interrupts atomically)
 *   ESP+36  ret addr   (first_entry_trampoline)
 *   ESP+40  EIP        \__ ring-3 IRET frame
 *   ESP+44  CS          |
 *   ESP+48  EFLAGS      |
 *   ESP+52  user ESP    |
 *   ESP+56  SS          /
 * ============================================================================ */

#include "arch/cpu.h"
#include "arch/abi.h"
#include "kernel/sched.h"
#include <stdint.h>

void arch_setup_first_stack(struct task_struct *task, uintptr_t user_entry,
                            uintptr_t user_esp, const registers_t *regs)
{
    uint32_t *kstack = (uint32_t *)(task->kernel_esp);

    if (regs) {
        /* ---- fork: clone the parent's trap frame, child returns 0 ---- */

        /* IRET frame (highest address = pushed first) */
        kstack--; *kstack = regs->ss;          /* SS            ESP+56 */
        kstack--; *kstack = regs->user_esp;    /* user ESP      ESP+52 */
        kstack--; *kstack = regs->eflags;      /* EFLAGS        ESP+48 */
        kstack--; *kstack = regs->cs;          /* CS            ESP+44 */
        kstack--; *kstack = regs->eip;         /* EIP           ESP+40 */

        /* Return address for switch_to's ret */
        kstack--; *kstack = (uint32_t)first_entry_trampoline; /* ESP+36 */

        /* EFLAGS for switch_to's popfl: IF=0 (see header comment) */
        kstack--; *kstack = 0x002;             /* EFLAGS        ESP+32 */

        /* popa frame: EAX=0 (child's fork return value), rest from parent */
        kstack--; *kstack = 0;                 /* EAX   ESP+28 */
        kstack--; *kstack = regs->ecx;         /* ECX   ESP+24 */
        kstack--; *kstack = regs->edx;         /* EDX   ESP+20 */
        kstack--; *kstack = regs->ebx;         /* EBX   ESP+16 */
        kstack--; *kstack = regs->esp_dummy;   /* dummy ESP+12 */
        kstack--; *kstack = regs->ebp;         /* EBP   ESP+8  */
        kstack--; *kstack = regs->esi;         /* ESI   ESP+4  */
        kstack--; *kstack = regs->edi;         /* EDI   ESP+0  (popa reads this first) */
    } else {
        /* ---- fresh start (spawn): standard ring-3 frame ---- */

        /* IRET frame (highest address = pushed first) */
        kstack--; *kstack = 0x23;              /* SS            ESP+56 */
        kstack--; *kstack = (uint32_t)user_esp;/* user ESP      ESP+52 */
        kstack--; *kstack = 0x202;             /* EFLAGS (iret) ESP+48 */
        kstack--; *kstack = 0x1B;              /* CS            ESP+44 */
        kstack--; *kstack = (uint32_t)user_entry; /* EIP        ESP+40 */

        /* Return address for switch_to's ret */
        kstack--; *kstack = (uint32_t)first_entry_trampoline; /* ESP+36 */

        /* EFLAGS for switch_to's popfl: IF=0 (iret enables interrupts) */
        kstack--; *kstack = 0x002;             /* EFLAGS        ESP+32 */

        /* popa frame: all zero */
        for (int i = 0; i < 8; i++) {
            kstack--; *kstack = 0;
        }
    }

    task->kernel_esp = (uint32_t)kstack;
}
