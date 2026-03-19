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
 * ------------------------------------------------------------------------- */

 .section .data
 rege:
 .align 16
 .skip 16

.global isr128
isr128:
    cli
    mov %eax, rege
    pusha                       /* Save: EAX ECX EDX EBX ESP EBP ESI EDI */
    pushl %ds
    pushl %es
    pushl %fs
    pushl %gs
    movl  %cr3, %eax            /* Save user CR3 */
    pushl %eax
    
    movw  $0x10, %ax            /* Switch to kernel segments */
    movw  %ax, %ds
    movw  %ax, %es
    movw  %ax, %fs
    movw  %ax, %gs
    
    /* Push arguments for syscall_handler(num, arg1, arg2, arg3, arg4, arg5)
     * From pusha: EAX=num, EBX=arg1, ECX=arg2, EDX=arg3, ESI=arg4, EDI=arg5 */
    pushl %edi                  /* arg5 */
    pushl %esi                  /* arg4 */
    pushl %edx                  /* arg3 */
    pushl %ecx                  /* arg2 */
    pushl %ebx                  /* arg1 */
    pushl rege                  /* num */
    
    call  syscall_handler
    
    addl  $24, %esp             /* Clean 6 args */
    movl  %eax, %edx            /* Save return value */
    
    popl  %eax                  /* Restore CR3 */
    movl  %eax, %cr3
    popl  %gs
    popl  %fs
    popl  %es
    popl  %ds
    
    /* Restore pusha registers, but replace EAX with return value */
    movl  %edx, 28(%esp)        /* Store return in saved EAX slot */
    popa
    iret

/* -------------------------------------------------------------------------
 * Exception common handler
 * ------------------------------------------------------------------------- */
isr_common:
    cli
    pusha
    pushl %ds
    pushl %es
    pushl %fs
    pushl %gs
    movw  $0x10, %ax
    movw  %ax, %ds
    movw  %ax, %es
    movw  %ax, %fs
    movw  %ax, %gs
    movw  %ax, %ss
    movl  kernel_page_table, %eax
    movl  %eax, %cr3
    pushl %esp
    call  panic_isr
1:  cli
    hlt
    jmp 1b