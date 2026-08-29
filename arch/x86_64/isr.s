/* ============================================================================
 * arch/x86_64/isr.s – CPU exception stubs (0-31), IRQ stubs (0-15),
 * the syscall/sysret entry, and the exception common handler.
 *
 * register frame pushed for panic_isr (see arch/x86_64/trap.c):
 *   rax rbx rcx rdx rsi rdi rbp r8 r9 r10 r11 r12 r13 r14 r15
 *   int_no err_code rip cs rflags rsp ss
 * ============================================================================ */

/* ---- Exception stubs ---- */
.macro ISR_NOERR num
.global isr\num
isr\num:
    pushq $0                 /* fake error code */
    pushq $\num              /* vector number   */
    jmp   isr_common
.endm

.macro ISR_ERR num
.global isr\num
isr\num:
    pushq $\num              /* vector number (CPU already pushed err code) */
    jmp   isr_common
.endm

.section .text
ISR_NOERR  0
ISR_NOERR  1
ISR_NOERR  2
ISR_NOERR  3
ISR_NOERR  4
ISR_NOERR  5
ISR_NOERR  6
ISR_NOERR  7
ISR_ERR    8
ISR_NOERR  9
ISR_ERR   10
ISR_ERR   11
ISR_ERR   12
ISR_ERR   13
ISR_ERR   14
ISR_NOERR 15
ISR_NOERR 16
ISR_ERR   17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_ERR   21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_ERR   29
ISR_ERR   30
ISR_NOERR 31

/* Stub address table for isr_init() (arch/x86_64/cpu.c). */
.section .data
.global isr_stub_table
isr_stub_table:
    .quad isr0,  isr1,  isr2,  isr3,  isr4,  isr5,  isr6,  isr7
    .quad isr8,  isr9,  isr10, isr11, isr12, isr13, isr14, isr15
    .quad isr16, isr17, isr18, isr19, isr20, isr21, isr22, isr23
    .quad isr24, isr25, isr26, isr27, isr28, isr29, isr30, isr31

/* ----------------------------------------------------------------------------
 * isr_common – save the GPR block and call panic_isr(&regs).
 * Stack on entry (after stub): [rsp+0]=int_no [rsp+8]=err_code
 *   [rsp+16]=rip [rsp+24]=cs [rsp+32]=rflags [rsp+40]=rsp [rsp+48]=ss
 * ---------------------------------------------------------------------------- */
isr_common:
    pushq %r15
    pushq %r14
    pushq %r13
    pushq %r12
    pushq %r11
    pushq %r10
    pushq %r9
    pushq %r8
    pushq %rbp
    pushq %rdi
    pushq %rsi
    pushq %rdx
    pushq %rcx
    pushq %rbx
    pushq %rax
    movq  %rsp, %rdi             /* regs = &rax */
    cld
    call  panic_isr
    popq  %rax
    popq  %rbx
    popq  %rcx
    popq  %rdx
    popq  %rsi
    popq  %rdi
    popq  %rbp
    popq  %r8
    popq  %r9
    popq  %r10
    popq  %r11
    popq  %r12
    popq  %r13
    popq  %r14
    popq  %r15
    addq  $16, %rsp              /* pop int_no + err_code */
    iretq

/* ----------------------------------------------------------------------------
 * IRQ stubs (0-15) – save all GPRs, dispatch, restore, iretq.
 * After the GPR block: [rsp+120]=rip [rsp+128]=cs [rsp+136]=rflags
 *                      [rsp+144]=rsp [rsp+152]=ss
 * ---------------------------------------------------------------------------- */
.macro IRQ_STUB num
.global irq\num
irq\num:
    cli
    pushq %r15
    pushq %r14
    pushq %r13
    pushq %r12
    pushq %r11
    pushq %r10
    pushq %r9
    pushq %r8
    pushq %rbp
    pushq %rdi
    pushq %rsi
    pushq %rdx
    pushq %rcx
    pushq %rbx
    pushq %rax
    movq  128(%rsp), %rsi        /* interrupted CS (arg2) */
    movl  $\num, %edi            /* IRQ number (arg1)     */
    call  irq_dispatch
    popq  %rax
    popq  %rbx
    popq  %rcx
    popq  %rdx
    popq  %rsi
    popq  %rdi
    popq  %rbp
    popq  %r8
    popq  %r9
    popq  %r10
    popq  %r11
    popq  %r12
    popq  %r13
    popq  %r14
    popq  %r15
    iretq
.endm

IRQ_STUB 0
IRQ_STUB 1
IRQ_STUB 2
IRQ_STUB 3
IRQ_STUB 4
IRQ_STUB 5
IRQ_STUB 6
IRQ_STUB 7
IRQ_STUB 8
IRQ_STUB 9
IRQ_STUB 10
/* ----------------------------------------------------------------------------
 * syscall_entry – invoked by the 'syscall' instruction.
 *
 * On entry: RCX = user RIP, R11 = user RFLAGS, RAX = syscall number,
 * RDI/RSI/RDX/R10/R8/R9 = args 1..6.  IF is masked (SFMASK).  The CPU does
 * NOT switch stacks, so we load RSP from TSS.rsp0 and save the user RSP in
 * a kernel global (single CPU).  We build the registers_t frame on the
 * kernel stack and call syscall_handler(regs, num, arg1..arg6), then
 * do_signal(regs), then return with sysretq.
 *
 * registers_t offsets: rax=0 rbx=8 rcx=16 rdx=24 rsi=32 rdi=40 rbp=48
 *   r8=56 r9=64 r10=72 r11=80 r12=88 r13=96 r14=104 r15=112
 *   rip=120 cs=128 rflags=136 rsp=144 ss=152
 * ---------------------------------------------------------------------------- */
.global syscall_entry
syscall_entry:
    /* Save user RSP and switch to the kernel stack (TSS.rsp0). */
    movq  %rsp, syscall_user_rsp(%rip)
    movq  tss + 4(%rip), %rsp        /* rsp0 (uint32_t reserved0, then rsp0) */

    subq  $160, %rsp                 /* reserve registers_t frame */

    /* IRET frame part (high offsets) */
    movq  %rcx, 120(%rsp)            /* rip      */
    movq  $0x23, 128(%rsp)           /* cs (0x20 | RPL3) */
    movq  %r11, 136(%rsp)            /* rflags   */
    movq  syscall_user_rsp(%rip), %rax
    movq  %rax, 144(%rsp)            /* rsp      */
    movq  $0x1B, 152(%rsp)           /* ss (0x18 | RPL3) */

    /* GPR part */
    movq  %rax, 0(%rsp)              /* rax (syscall number) -> return slot */
    movq  %rbx, 8(%rsp)
    movq  %rcx, 16(%rsp)
    movq  %rdx, 24(%rsp)
    movq  %rsi, 32(%rsp)
    movq  %rdi, 40(%rsp)
    movq  %rbp, 48(%rsp)
    movq  %r8,  56(%rsp)
    movq  %r9,  64(%rsp)
    movq  %r10, 72(%rsp)
    movq  %r11, 80(%rsp)
    movq  %r12, 88(%rsp)
    movq  %r13, 96(%rsp)
    movq  %r14, 104(%rsp)
    movq  %r15, 112(%rsp)

    /* syscall_handler(regs, num, arg1, arg2, arg3, arg4, arg5, arg6) */
    movq  %rsp, %rdi                 /* regs   */
    movq  0(%rsp), %rsi              /* num    */
    movq  40(%rsp), %rdx             /* arg1 (rdi)  */
    movq  32(%rsp), %rcx             /* arg2 (rsi)  */
    movq  24(%rsp), %r8              /* arg3 (rdx)  */
    movq  72(%rsp), %r9              /* arg4 (r10)  */
    pushq 64(%rsp)                   /* arg6 (r9)   */
    pushq 56(%rsp)                   /* arg5 (r8)   */
    call  syscall_handler
    addq  $16, %rsp

    /* Store the return value in the frame's rax slot. */
    movq  %rax, 0(%rsp)

    /* Deliver pending signals (may kill the task or rewrite rip/rsp). */
    movq  %rsp, %rdi
    call  do_signal

    /* Restore user context and sysret. */
    movq  0(%rsp), %rax              /* return value  */
    movq  120(%rsp), %rcx            /* user rip      */
    movq  136(%rsp), %r11            /* user rflags   */
    movq  144(%rsp), %rsp            /* user rsp      */
    sysretq

/* Per-CPU scratch for the syscall entry (single CPU for now). */
.section .bss
.align 8
syscall_user_rsp:
    .skip 8

IRQ_STUB 11
IRQ_STUB 12
IRQ_STUB 13
IRQ_STUB 14
IRQ_STUB 15
