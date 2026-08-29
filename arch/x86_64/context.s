/* ============================================================================
 * arch/x86_64/context.s – context switching and userspace entry
 *
 * switch_to(uintptr_t *old_rsp, uintptr_t new_rsp, uintptr_t new_cr3)
 *   Only RSP and CR3 are swapped: the C compiler has already saved the
 *   task's callee-saved registers on its kernel stack, so returning is a
 *   pure stack swap.  A task scheduled for the first time rets into
 *   first_entry_trampoline() (frame built by arch_setup_first_stack).
 * ============================================================================ */

/* switch_to: save RSP to *rdi, load RSP from rsi, load CR3 from rdx, ret */
.global switch_to
switch_to:
    movq %rsp, (%rdi)          /* *old_rsp = current RSP */
    movq %rsi, %rsp            /* RSP = new_rsp          */
    movq %rdx, %cr3            /* CR3  = new_cr3         */
    ret

/* ----------------------------------------------------------------------------
 * first_entry_trampoline
 *
 * Reached via ret from switch_to when a task is first scheduled.  The
 * kernel stack holds the arch_setup_first_stack() frame:
 *   [rsp+0]   RAX ... [rsp+112] R15   (popped below)
 *   [rsp+120] RIP  [rsp+128] CS=0x23  [rsp+136] RFLAGS(IF=1)
 *   [rsp+144] RSP  [rsp+152] SS=0x1B
 * We restore the GPRs (RAX=0 for a fork child) and iretq atomically enters
 * ring 3 with interrupts enabled.
 * ---------------------------------------------------------------------------- */
.global first_entry_trampoline
first_entry_trampoline:
    popq %rax
    popq %rbx
    popq %rcx
    popq %rdx
    popq %rsi
    popq %rdi
    popq %rbp
    popq %r8
    popq %r9
    popq %r10
    popq %r11
    popq %r12
    popq %r13
    popq %r14
    popq %r15
    iretq

/* ----------------------------------------------------------------------------
 * enter_userspace(cr3=rdi, entry=rsi, user_rsp=rdx)
 *
 * Switch from kernel to user mode (never returns).  Kernel stays mapped
 * (supervisor-only); we build an iretq frame on the current kernel stack.
 * ---------------------------------------------------------------------------- */
.global enter_userspace
enter_userspace:
    cli
    movq %rdi, %cr3            /* load user page tables */

    pushq $0x1B                /* SS  (user data 0x18 | RPL3) */
    pushq %rdx                 /* user RSP */
    pushfq
    popq  %rax
    orq   $0x200, %rax         /* set IF */
    pushq %rax                 /* RFLAGS */
    pushq $0x23                /* CS (user code 0x20 | RPL3) */
    pushq %rsi                 /* entry RIP */
    iretq

.hang:
    hlt
    jmp .hang
