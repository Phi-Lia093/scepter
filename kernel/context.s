/* ============================================================================
 * Context Switching and Userspace Entry (x86 Assembly)
 * ============================================================================ */

.global enter_userspace
.global switch_to
.global first_entry_trampoline

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
    orl $0x200, %eax             /* Set IF (interrupt enable) flag */
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

/* ============================================================================
 * switch_to(uint32_t *old_esp, uint32_t new_esp, uint32_t new_cr3)
 *
 * Low-level context switch between tasks:
 * 1. Save current task's context (registers + EFLAGS)
 * 2. Save current ESP to *old_esp
 * 3. Load new task's ESP
 * 4. Switch to new task's CR3 (page directory)
 * 5. Restore new task's context
 * 6. Return (now running as new task)
 * ============================================================================ */

switch_to:
    /* Save current context */
    pushfl                       /* Save EFLAGS */
    pusha                        /* Save: EAX ECX EDX EBX ESP EBP ESI EDI */
    
    /* Get parameters from stack
     * Stack: [EFLAGS] [EAX] [ECX] [EDX] [EBX] [ESP] [EBP] [ESI] [EDI] [ret] [old_esp] [new_esp] [new_cr3] */
    movl 40(%esp), %eax          /* old_esp pointer */
    movl 44(%esp), %edx          /* new_esp value */
    movl 48(%esp), %ecx          /* new_cr3 value */
    
    /* Save old ESP */
    movl %esp, (%eax)            /* *old_esp = current ESP */
    
    /* Switch to new stack */
    movl %edx, %esp              /* ESP = new_esp */
    
    /* Switch page directory */
    movl %ecx, %cr3              /* CR3 = new_cr3 */
    
    /* Restore new context */
    popa                         /* Restore: EDI ESI EBP ESP EBX EDX ECX EAX */
    popfl                        /* Restore EFLAGS */
    
    ret                          /* Return to new task */

/* ============================================================================
 * first_entry_trampoline
 *
 * Called (via ret) when a task is scheduled for the FIRST TIME.
 * The kernel stack already has a ring-3 IRET frame waiting:
 *   [ESP+0]  EIP  (user entry point)
 *   [ESP+4]  CS   (0x1B = user code, RPL=3)
 *   [ESP+8]  EFLAGS
 *   [ESP+12] ESP  (user stack pointer)
 *   [ESP+16] SS   (0x23 = user data, RPL=3)
 *
 * Just execute IRET to drop into ring 3.
 * ============================================================================ */

first_entry_trampoline:
    /* Set user data segments before iret */
    movl $0x23, %eax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    
    iret                         /* Jump
