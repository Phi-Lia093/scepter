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
        /* GPR block (popped by first_entry_trampoline).  Pushed in
         * REVERSE pop order: the trampoline pops RAX first, so RAX must
         * be at the lowest address (pushed last).  RAX=0 so the child
         * sees fork() == 0; the rest mirror the parent. */
        kstack--; *kstack = regs->r15;
        kstack--; *kstack = regs->r14;
        kstack--; *kstack = regs->r13;
        kstack--; *kstack = regs->r12;
        kstack--; *kstack = regs->r11;
        kstack--; *kstack = regs->r10;
        kstack--; *kstack = regs->r9;
        kstack--; *kstack = regs->r8;
        kstack--; *kstack = regs->rbp;
        kstack--; *kstack = regs->rdi;
        kstack--; *kstack = regs->rsi;
        kstack--; *kstack = regs->rdx;
        kstack--; *kstack = regs->rcx;
        kstack--; *kstack = regs->rbx;
        kstack--; *kstack = 0;               /* RAX (pushed last = popped first) */
    } else {
        /* ---- fresh start (spawn) ---- */
        kstack--; *kstack = 0x1B;            /* SS            */
        kstack--; *kstack = user_esp;        /* user RSP      */
        kstack--; *kstack = 0x202;           /* RFLAGS (IF=1) */
        kstack--; *kstack = 0x23;            /* CS            */
        kstack--; *kstack = user_entry;      /* RIP           */
        for (int i = 0; i < 15; i++)
            kstack--, *kstack = 0;           /* GPRs = 0 */
    }

    /* switch_to() rets to first_entry_trampoline, which pops the GPR
     * block and iretq's. */
    kstack--; *kstack = (uintptr_t)first_entry_trampoline;

    task->kernel_esp = (uintptr_t)kstack;
}
