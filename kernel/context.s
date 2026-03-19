/* ============================================================================
 * Context Switching and Userspace Entry (x86 Assembly)
 * ============================================================================ */

.global enter_userspace

/* ============================================================================
 * enter_userspace(uint32_t cr3, uint32_t entry)
 *
 * Switch from kernel mode to user mode by:
 * 1. Loading user CR3 (kernel remains mapped as supervisor-only)
 * 2. Setting up user segments
 * 3. Building IRET frame
 * 4. Executing IRET to jump to userspace
 *
 * IMPORTANT: This function DOES NOT RETURN!
 * ============================================================================ */

enter_userspace:
    cli                          /* Disable interrupts */
    
    /* Get parameters */
    movl 4(%esp), %eax           /* cr3 (user page directory physical address) */
    movl 8(%esp), %ecx           /* entry point */
    
    /* Load user CR3 - kernel is still mapped (supervisor-only) */
    movl %eax, %cr3
    
    
    /* Set up user data segment (GDT entry 4, RPL=3) */
    movl $0x23, %eax             /* 0x20 (GDT offset) | 0x3 (RPL=3) */
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    
    /* Build IRET frame on kernel stack
     * IRET pops: EIP, CS, EFLAGS, ESP, SS
     */
    
    /* Push user stack segment selector (SS) */
    pushl $0x23                  /* User data segment (RPL=3) */
    
    /* Push initial user stack pointer (ESP) */
    pushl %ecx                   /* Use entry point (no real stack yet) */
    
    /* Push EFLAGS with interrupts enabled */
    pushfl
    popl %eax
    // orl $0x200, %eax             /* Set IF (interrupt enable) flag */
    pushl %eax
    
    /* Push user code segment selector (CS) */
    pushl $0x1B                  /* 0x18 (GDT offset) | 0x3 (RPL=3) */
    
    /* Push entry point (EIP) */
    pushl %ecx
    
    /* Execute IRET to switch to Ring 3 */
    iret
    
    /* Should NEVER reach here! */
.loop:
    hlt
    jmp .loop