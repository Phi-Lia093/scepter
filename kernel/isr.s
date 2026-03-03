/* =========================================================================
 * isr.s – CPU exception stubs (vectors 0–31)
 *
 * Each stub:
 *   1. Pushes a dummy error code (for exceptions that don't push one)
 *   2. Pushes the exception number
 *   3. Saves all GPRs (pusha) and segment registers
 *   4. Switches to kernel data segment and kernel CR3
 *   5. Calls panic_isr(regs_t *r)  — never returns
 * ========================================================================= */

/* Macro for exceptions WITHOUT a hardware error code */
.macro ISR_NOERR num
.global isr\num
isr\num:
    pushl $0            /* dummy error code */
    pushl $\num         /* exception number */
    jmp   isr_common
.endm

/* Macro for exceptions WITH a hardware error code */
.macro ISR_ERR num
.global isr\num
isr\num:
    /* CPU already pushed error code */
    pushl $\num         /* exception number */
    jmp   isr_common
.endm

/* -------------------------------------------------------------------------
 * Exception stubs (Intel SDM Vol.3 Table 6-1)
 * ------------------------------------------------------------------------- */
.section .text

ISR_NOERR  0   /* #DE  Divide Error                  */
ISR_NOERR  1   /* #DB  Debug                         */
ISR_NOERR  2   /*      NMI                           */
ISR_NOERR  3   /* #BP  Breakpoint                    */
ISR_NOERR  4   /* #OF  Overflow                      */
ISR_NOERR  5   /* #BR  BOUND Range Exceeded           */
ISR_NOERR  6   /* #UD  Invalid Opcode                */
ISR_NOERR  7   /* #NM  Device Not Available           */
ISR_ERR    8   /* #DF  Double Fault          (err=0) */
ISR_NOERR  9   /*      Coprocessor Seg Overrun       */
ISR_ERR   10   /* #TS  Invalid TSS                   */
ISR_ERR   11   /* #NP  Segment Not Present           */
ISR_ERR   12   /* #SS  Stack-Segment Fault           */
ISR_ERR   13   /* #GP  General Protection            */
ISR_ERR   14   /* #PF  Page Fault                    */
ISR_NOERR 15   /*      Reserved                      */
ISR_NOERR 16   /* #MF  x87 FPU Error                 */
ISR_ERR   17   /* #AC  Alignment Check               */
ISR_NOERR 18   /* #MC  Machine Check                 */
ISR_NOERR 19   /* #XM  SIMD FP Exception             */
ISR_NOERR 20   /* #VE  Virtualization Exception      */
ISR_ERR   21   /* #CP  Control Protection            */
ISR_NOERR 22   /*      Reserved                      */
ISR_NOERR 23   /*      Reserved                      */
ISR_NOERR 24   /*      Reserved                      */
ISR_NOERR 25   /*      Reserved                      */
ISR_NOERR 26   /*      Reserved                      */
ISR_NOERR 27   /*      Reserved                      */
ISR_NOERR 28   /* #HV  Hypervisor Injection           */
ISR_ERR   29   /* #VC  VMM Communication             */
ISR_ERR   30   /* #SX  Security Exception            */
ISR_NOERR 31   /*      Reserved                      */

/* -------------------------------------------------------------------------
 * IRQ stubs – save context, call C handler, restore, iret
 * ------------------------------------------------------------------------- */

.macro IRQ_STUB num, handler
.global irq\num
irq\num:
    pusha
    movw  %ds, %ax
    movw  %es, %ax
    movw  %fs, %ax
    movw  %gs, %ax
    movw  $0x10, %ax
    movw  %ax, %ds
    movw  %ax, %es
    movw  %ax, %fs
    movw  %ax, %gs
    movl  kernel_page_table, %eax
    movl  %eax, %cr3
    call  \handler
    movw  %ax, %gs
    movw  %ax, %fs
    movw  %ax, %es
    movw  %ax, %ds
    popa
    iret
.endm

IRQ_STUB 0, pit_isr   /* IRQ0 – PIT timer */
IRQ_STUB 1, kbd_isr   /* IRQ1 – Keyboard */

/* -------------------------------------------------------------------------
 * Common handler: save context, switch to kernel env, call panic_isr
 * ------------------------------------------------------------------------- */
isr_common:
    pusha                       /* save EAX ECX EDX EBX ESP EBP ESI EDI */

    xorl %eax, %eax
    /* Save segment registers */
    movw  %ds, %ax
    pushl %eax
    movw  %es, %ax
    pushl %eax
    movw  %fs, %ax
    pushl %eax
    movw  %gs, %ax
    pushl %eax

    /* Switch to kernel data segment */
    movw  $0x10, %ax
    movw  %ax, %ds
    movw  %ax, %es
    movw  %ax, %fs
    movw  %ax, %gs
    movw  %ax, %ss

    /* Switch to kernel page directory */
    movl  kernel_page_table, %eax
    movl  %eax, %cr3

    /* Pass pointer to saved frame as argument to panic_isr */
    pushl %esp
    call  panic_isr

    /* Should never reach here – panic halts the CPU */
1:  cli
    hlt
    jmp 1b
