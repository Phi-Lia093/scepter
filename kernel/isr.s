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
.align 16
syscall_regs:
    .skip 80    /* sizeof(registers_t) = 20 * 4 = 80 bytes */

.global isr128
isr128:
    cli
    
    /* Save all registers directly to static buffer
     * This avoids complex stack calculations
     * Layout matches registers_t: EDI, ESI, EBP, ESP, EBX, EDX, ECX, EAX,
     *                              GS, FS, ES, DS, CR3, EIP, CS, EFLAGS, ESP, SS */
    
    /* Save general-purpose registers (offsets match pusha order) */
    movl %edi, syscall_regs + 0       /* EDI offset 0 */
    movl %esi, syscall_regs + 4       /* ESI offset 4 */
    movl %ebp, syscall_regs + 8       /* EBP offset 8 */
    movl %esp, syscall_regs + 12      /* ESP offset 12 */
    movl %ebx, syscall_regs + 16      /* EBX offset 16 */
    movl %edx, syscall_regs + 20      /* EDX offset 20 */
    movl %ecx, syscall_regs + 24      /* ECX offset 24 */
    movl %eax, syscall_regs + 28      /* EAX offset 28 */
    
    /* Save segment registers */
    movw %gs, %ax
    movl %eax, syscall_regs + 32      /* GS offset 32 */
    movw %fs, %ax
    movl %eax, syscall_regs + 36      /* FS offset 36 */
    movw %es, %ax
    movl %eax, syscall_regs + 40      /* ES offset 40 */
    movw %ds, %ax
    movl %eax, syscall_regs + 44      /* DS offset 44 */
    
    /* Save CR3 */
    movl %cr3, %eax
    movl %eax, syscall_regs + 48      /* CR3 offset 48 */
    
    /* Save IRET frame from stack (CPU pushed: EIP, CS, EFLAGS, ESP, SS) */
    movl 0(%esp), %eax                /* EIP */
    movl %eax, syscall_regs + 52
    movl 4(%esp), %eax                /* CS */
    movl %eax, syscall_regs + 56
    movl 8(%esp), %eax                /* EFLAGS */
    movl %eax, syscall_regs + 60
    movl 12(%esp), %eax               /* User ESP */
    movl %eax, syscall_regs + 64
    movl 16(%esp), %eax               /* SS */
    movl %eax, syscall_regs + 68
    
    /* Switch to kernel segments */
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    
    /* Get syscall arguments from saved registers */
    movl syscall_regs + 28, %eax      /* num (from EAX) */
    movl syscall_regs + 16, %ebx      /* arg1 (from EBX) */
    movl syscall_regs + 24, %ecx      /* arg2 (from ECX) */
    movl syscall_regs + 20, %edx      /* arg3 (from EDX) */
    movl syscall_regs + 4, %esi       /* arg4 (from ESI) */
    movl syscall_regs + 0, %edi       /* arg5 (from EDI) */
    
    /* Push arguments for syscall_handler(regs, num, arg1, arg2, arg3, arg4, arg5) */
    pushl %edi                        /* arg5 */
    pushl %esi                        /* arg4 */
    pushl %edx                        /* arg3 */
    pushl %ecx                        /* arg2 */
    pushl %ebx                        /* arg1 */
    pushl %eax                        /* num */
    pushl $syscall_regs               /* regs - simple static address! */
    
    call syscall_handler
    
    addl $28, %esp                    /* Clean 7 args */
    
    /* Return value is in EAX - save it */
    movl %eax, %edx
    
    /* Restore CR3 */
    movl syscall_regs + 48, %eax
    movl %eax, %cr3
    
    /* Restore segment registers */
    movl syscall_regs + 32, %eax
    movw %ax, %gs
    movl syscall_regs + 36, %eax
    movw %ax, %fs
    movl syscall_regs + 40, %eax
    movw %ax, %es
    movl syscall_regs + 44, %eax
    movw %ax, %ds
    
    /* Restore general-purpose registers, but put return value in EAX */
    movl syscall_regs + 0, %edi
    movl syscall_regs + 4, %esi
    movl syscall_regs + 8, %ebp
    /* Skip ESP at offset 12 */
    movl syscall_regs + 16, %ebx
    /* Skip EDX - we need it for return value */
    movl syscall_regs + 24, %ecx
    movl %edx, %eax                   /* Return value in EAX */
    movl syscall_regs + 20, %edx      /* Now restore EDX */
    
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
