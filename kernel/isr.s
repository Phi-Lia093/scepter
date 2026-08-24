/* =========================================================================
 * isr.s – CPU exception stubs (vectors 0–31)
 * ========================================================================= */

.macro ISR_NOERR num
.global isr\num
isr\num:
    pushl $0
    pushl $\num
    jmp   isr_common
.endm

.macro ISR_ERR num
.global isr\num
isr\num:
    pushl $\num
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

/* -------------------------------------------------------------------------
 * IRQ stubs
 * ------------------------------------------------------------------------- */

.macro IRQ_STUB num, handler
.global irq\num
irq\num:
    cli
    pusha
    pushl %ds
    pushl %es
    pushl %fs
    pushl %gs
    movl  %cr3, %eax
    pushl %eax
    movw  $0x10, %ax
    movw  %ax, %ds
    movw  %ax, %es
    movw  %ax, %fs
    movw  %ax, %gs
    call  \handler
    popl  %eax
    movl  %eax, %cr3
    popl  %gs
    popl  %fs
    popl  %es
    popl  %ds
    popa
    iret
.endm

IRQ_STUB 0, pit_isr
IRQ_STUB 1, kbd_isr

/* -------------------------------------------------------------------------
 * Syscall stub (int 0x80)
 *
 * IMPORTANT: the register frame (registers_t) is built ON THE CURRENT
 * PROCESS'S KERNEL STACK, NOT in a global buffer. This is essential for
 * blocking syscalls (e.g. wait()): a process can be descheduled while it
 * is inside the syscall handler and resume much later. A per-process
 * kernel stack frame survives that, whereas a single global buffer would
 * be overwritten by every other process's syscalls in the meantime,
 * corrupting the blocked process's return state.
 * ------------------------------------------------------------------------- */

.global isr128
isr128:
    cli

    /* At entry the CPU has pushed the IRET frame from ring 3:
     *   [ESP+0]=EIP [ESP+4]=CS [ESP+8]=EFLAGS
     *   [ESP+12]=user_esp [ESP+16]=SS
     *
     * We build registers_t (see kernel/syscall.h) on the kernel stack
     * in EXACT struct order:
     *   [ESP+0..28]  edi esi ebp esp_dummy ebx edx ecx eax
     *   [ESP+32..44] gs fs es ds
     *   [ESP+48]     cr3
     *   [ESP+52..68] eip cs eflags user_esp ss  (the CPU IRET frame)
     *
     * This must live on THIS process's kernel stack (not a global buffer):
     * a blocking syscall (wait) can deschedule the process mid-handler and
     * resume much later; only the per-process stack survives that.
     */

    /* Save EAX (syscall number) by pushing it BELOW the CPU frame
     * (safe: inside the kernel stack). Then read CR3 into EAX and swap:
     * the CR3 value goes into the stack slot (which becomes the frame's
     * CR3 field, right below the CPU frame) and EAX gets the syscall
     * number back.  NO writes above the CPU frame: the byte at the very
     * top of the kernel stack belongs to whatever page the allocator
     * placed after it (e.g. a page table), and must never be touched. */
    pushl %eax                     /* [ESP-4] temp = syscall number */
    movl %cr3, %eax
    xchgl %eax, (%esp)             /* [ESP-4] = cr3, EAX = syscall number */

    pushl %ds                      /* ds    -> [base+44] */
    pushl %es                      /* es    -> [base+40] */
    pushl %fs                      /* fs    -> [base+36] */
    pushl %gs                      /* gs    -> [base+32] */

    /* Push the GPR frame (EAX already holds the syscall number) */
    pushl %eax                     /* eax   -> [base+28] */
    pushl %ecx                     /* ecx   -> [base+24] */
    pushl %edx                     /* edx   -> [base+20] */
    pushl %ebx                     /* ebx   -> [base+16] */
    pushl %esp                     /* dummy -> [base+12] */
    pushl %ebp                     /* ebp   -> [base+8]  */
    pushl %esi                     /* esi   -> [base+4]  */
    pushl %edi                     /* edi   -> [base+0]  */

    /* ESP now points at the complete registers_t frame. */

    /* Switch to kernel data segments */
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs

    /* Load syscall arguments from the frame */
    movl 28(%esp), %eax            /* num   (EAX) */
    movl 16(%esp), %ebx            /* arg1  (EBX) */
    movl 24(%esp), %ecx            /* arg2  (ECX) */
    movl 20(%esp), %edx            /* arg3  (EDX) */
    movl 4(%esp), %esi             /* arg4  (ESI) */
    movl 0(%esp), %edi             /* arg5  (EDI) */

    /* syscall_handler(regs, num, arg1, arg2, arg3, arg4, arg5) */
    pushl %edi                     /* arg5 */
    pushl %esi                     /* arg4 */
    pushl %edx                     /* arg3 */
    pushl %ecx                     /* arg2 */
    pushl %ebx                     /* arg1 */
    pushl %eax                     /* num */
    lea 24(%esp), %eax             /* regs = base (ESP+24 after 6 pushes) */
    pushl %eax                     /* regs */

    call syscall_handler

    addl $28, %esp                 /* clean 7 args; ESP = base again */

    /* Store return value into the EAX slot of the frame */
    movl %eax, 28(%esp)

    /* Restore CR3 */
    movl 48(%esp), %eax
    movl %eax, %cr3

    /* Restore segment registers */
    movl 32(%esp), %eax
    movw %ax, %gs
    movl 36(%esp), %eax
    movw %ax, %fs
    movl 40(%esp), %eax
    movw %ax, %es
    movl 44(%esp), %eax
    movw %ax, %ds

    /* Restore general-purpose registers (EAX gets the return value).
     * After popa, ESP points at GS; skip GS FS ES DS CR3 (20 bytes),
     * then iret pops EIP CS EFLAGS user_esp SS. */
    popa
    addl $20, %esp
    iret

/* -------------------------------------------------------------------------
 * Exception common handler
 * Stack layout on entry (from bottom to top):
 *   [SS]          - if privilege change
 *   [ESP]         - if privilege change  
 *   EFLAGS
 *   CS
 *   EIP
 *   error_code    - pushed by CPU or stub
 *   int_num       - pushed by stub
 * ------------------------------------------------------------------------- */
isr_common:
    cli
    pusha                       /* Push: EAX ECX EDX EBX ESP EBP ESI EDI */
    pushl %ds
    pushl %es
    pushl %fs
    pushl %gs
    
    movw  $0x10, %ax            /* Load kernel data segment */
    movw  %ax, %ds
    movw  %ax, %es
    movw  %ax, %fs
    movw  %ax, %gs
    
    /* Save user CR3 and switch to kernel page table */
    movl  %cr3, %eax
    pushl %eax
    movl  kernel_page_table, %eax
    movl  %eax, %cr3
    
    /* Call C handler with pointer to register state */
    pushl %esp
    call  panic_isr
    addl  $4, %esp
    
    /* Restore user CR3 */
    popl  %eax
    movl  %eax, %cr3
    
    /* Restore segment registers */
    popl  %gs
    popl  %fs
    popl  %es
    popl  %ds
    
    /* Restore general registers */
    popa
    
    /* Remove error code and interrupt number from stack */
    addl  $8, %esp
    
    /* Return to interrupted code */
    iret
