/* ============================================================================
 * Task context setup (x86_64)
 *
 * Builds the initial kernel-stack frame for a task being scheduled for the
 * first time.  switch_to() only swaps RSP/CR3 and rets; the frame is:
 *
 *   [esp+0]  first_entry_trampoline   (ret target)
 *   [esp+8]  RIP            \__ ring-3 iretq frame
 *   [esp+16] CS              |
 *   [esp+24] RFLAGS          |
 *   [esp+32] user RSP        |
 *   [esp+40] SS              /
 * ============================================================================ */

#include "arch/cpu.h"
#include "arch/abi.h"
#include "kernel/sched.h"
#include <stdint.h>

void arch_setup_first_stack(struct task_struct *task, uintptr_t user_entry,
                            uintptr_t user_esp, const registers_t *regs)
{
    uintptr_t *kstack = (uintptr_t *)(task->kernel_esp);

    if (regs) {
        /* ---- fork: clone the parent's user context from its frame ---- */
        kstack--; *kstack = regs->ss;        /* SS            */
        kstack--; *kstack = regs->rsp;       /* user RSP      */
        kstack--; *kstack = regs->rflags;    /* RFLAGS        */
        kstack--; *kstack = regs->cs;        /* CS            */
        kstack--; *kstack = regs->rip;       /* RIP           */
    } else {
        /* ---- fresh start (spawn) ---- */
        kstack--; *kstack = 0x1B;            /* SS            */
        kstack--; *kstack = user_esp;        /* user RSP      */
        kstack--; *kstack = 0x202;           /* RFLAGS (IF=1) */
        kstack--; *kstack = 0x23;            /* CS            */
        kstack--; *kstack = user_entry;      /* RIP           */
    }

    /* switch_to() rets to first_entry_trampoline, which iretq's. */
    kstack--; *kstack = (uintptr_t)first_entry_trampoline;

    task->kernel_esp = (uintptr_t)kstack;
}
