/* ============================================================================
 * arch/x86_64/boot.s
 *
 * Multiboot2 header + 32-bit trampoline into long mode.
 *
 * GRUB2 (multiboot2) loads the ELF64 and enters at _start in 32-bit
 * protected mode with paging disabled.  This trampoline:
 *   1. saves the multiboot2 magic + boot info pointer,
 *   2. builds 4-level page tables: identity [0,1 GB) and the higher-half
 *      kernel [KERNEL_VMA, KERNEL_VMA+1 GB) -> phys 2 MB,
 *   3. enables PAE + EFER.LME + paging,
 *   4. loads a minimal 64-bit GDT and far-jumps to the 64-bit entry.
 * ============================================================================ */

/* ----------------------------------------------------------------------------
 * Multiboot2 header (must be in the first 64 KB of the file)
 * ---------------------------------------------------------------------------- */
.section .multiboot2, "a"
.align 8
multiboot2_header:
    .long 0xE85250D6                        /* magic                          */
    .long 0                                 /* architecture: i386 (32-bit)    */
    .long mb2_hdr_end - multiboot2_header   /* header length                  */
    .long -(0xE85250D6 + 0 + (mb2_hdr_end - multiboot2_header)) /* checksum   */
    /* Request the memory map (type 6). */
    .align 8
    .word 6
    .word 0
    .long 16
    .long 0
    /* End tag. */
    .align 8
    .word 0
    .word 0
    .long 8
mb2_hdr_end:

/* ----------------------------------------------------------------------------
 * 32-bit trampoline (linked at physical 1 MB, identity-mapped)
 * ---------------------------------------------------------------------------- */
.section .text.boot, "ax"
.code32
.global _start
_start:
    cli

    /* Save multiboot2 magic (eax) and boot info pointer (ebx) FIRST,
     * before any debug output clobbers them. */
    movl %eax, saved_magic
    movl %ebx, saved_info

    /* ---- Debug: initialize COM1 and write '0' (serial is the only
     * usable console before the kernel brings up its own console). ---- */
    movl $0x3F8, %edx
    addl $1, %edx                   /* IER */
    movb $0x00, %al
    outb %al, %dx
    movl $0x3F8, %edx
    addl $3, %edx                   /* LCR: DLAB on */
    movb $0x80, %al
    outb %al, %dx
    movl $0x3F8, %edx               /* DLL = 1 (115200) */
    movb $0x01, %al
    outb %al, %dx
    movl $0x3F8, %edx
    addl $1, %edx                   /* DLM = 0 */
    movb $0x00, %al
    outb %al, %dx
    movl $0x3F8, %edx
    addl $3, %edx                   /* LCR: 8N1, DLAB off */
    movb $0x03, %al
    outb %al, %dx
    movl $0x3F8, %edx
    addl $2, %edx                   /* FCR: enable FIFO */
    movb $0xC7, %al
    outb %al, %dx
    movl $0x3F8, %edx               /* THR: send '0' */
    movb $'0', %al
    outb %al, %dx

    /* Debug: '1' = about to build page tables. */
    movb $'1', %al
    call ser_putc32

    /* ---- Zero the page tables: pml4, pdpt0, pd0, pd_hi ---- */
    movl $pml4, %edi
    xorl %eax, %eax
    movl $((4096 * 4) / 4), %ecx
    rep stosl

    /* ---- PML4[0] -> pdpt0 ; PDPT0[0] -> pd0 ---- */
    movl $pdpt0, %eax
    orl  $0x3, %eax                       /* present | writable */
    movl %eax, pml4 + 0*8

    movl $pd0, %eax
    orl  $0x3, %eax
    movl %eax, pdpt0 + 0*8

    /* ---- PML4[511] -> pdpt_hi ; PDPT_hi[510] -> pd_hi ----
     * KERNEL_VMA = 0xFFFFFFFF80000000 sits at PML4[511].PDPT[510].PD[0]. */
    movl $pdpt_hi, %eax
    orl  $0x3, %eax
    movl %eax, pml4 + 511*8

    movl $pd_hi, %eax
    orl  $0x3, %eax
    movl %eax, pdpt_hi + 510*8

    /* ---- pd0[0..511] = identity map [0, 1 GB) with 2 MB pages ---- */
    movl $pd0, %edi
    movl $0x00000083, %eax                /* present | writable | PS        */
    movl $512, %ecx
1:  movl %eax, (%edi)
    movl $0, 4(%edi)                      /* high dword */
    addl $0x200000, %eax                  /* next 2 MB */
    addl $8, %edi
    loop 1b

    /* ---- pd_hi[0..511] = direct map: phys [0, 1 GB) -> KERNEL_VMA ----
     * The kernel image (loaded at phys 2 MB) is thus at KERNEL_VMA+2MB,
     * and PHYS_TO_VIRT(0xB8000) = KERNEL_VMA+0xB8000 works for all RAM. */
    movl $pd_hi, %edi
    movl $0x00000083, %eax                /* present | writable | PS */
    movl $512, %ecx
2:  movl %eax, (%edi)
    movl $0, 4(%edi)                      /* high dword */
    addl $0x200000, %eax
    addl $8, %edi
    loop 2b

    /* ---- Enable paging ---- */
    movb $'2', %al
    call ser_putc32

    movl $pml4, %eax
    movl %eax, %cr3                       /* CR3 = pml4 (phys == VMA) */

    movb $'3', %al
    call ser_putc32

    movl %cr4, %eax
    orl  $(1 << 5), %eax                  /* PAE */
    movl %eax, %cr4

    movb $'4', %al
    call ser_putc32

    movl $0xC0000080, %ecx                /* EFER */
    rdmsr
    orl  $(1 << 8), %eax                  /* LME */
    wrmsr

    movb $'5', %al
    call ser_putc32

    movl %cr0, %eax
    orl  $(1 << 31), %eax                 /* PG */
    movl %eax, %cr0

    movb $'6', %al
    call ser_putc32

    /* Load a minimal 64-bit GDT and far-jump into long mode.
     * The jump target is the PHYSICAL address of _start_64. */
    lgdt (gdt64_ptr)
    ljmp $0x08, $_start_64_phys

    /* Send AL to COM1 (32-bit helper for trampoline debugging). */
ser_putc32:
    pushl %edx
    pushl %eax
    movl $0x3F8, %edx
    addl $5, %edx                 /* LSR */
5:  inb %dx, %al
    testb $0x20, %al              /* THR empty? */
    jz 5b
    movl $0x3F8, %edx
    popl %eax
    outb %al, %dx
    popl %edx
    ret


/* ----------------------------------------------------------------------------
 * 64-bit kernel entry
 *
 * The 32-bit trampoline far-jumps to the PHYSICAL address of _start_64
 * (identity-mapped).  The kernel image is linked at KERNEL_VMA, so we
 * immediately jump to the higher-half virtual address (mapped by pd_hi)
 * before any RIP-relative code runs.
 * ---------------------------------------------------------------------------- */
.section .text, "ax"
.code64
.global _start_64
_start_64:
    movabs $_start_64_v, %rax
    jmp *%rax

_start_64_v:
    /* Reload segment registers with the kernel data segment. */
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    movw %ax, %ss

    /* Early debug: send 'A' to COM1. */
    movb $'A', %cl
    call ser_putc

    /* Switch to the boot stack (low memory, identity-mapped). */
    leaq boot_stack_top(%rip), %rsp

    movb $'B', %cl
    call ser_putc

    /* Zero the kernel .bss. */
    leaq _bss_start_v(%rip), %rdi
    leaq kernel_end(%rip), %rcx
    subq %rdi, %rcx
    xorl %eax, %eax
    rep stosb

    movb $'C', %cl
    call ser_putc

    /* Publish the boot PML4 physical address (its VMA == phys). */
    movq $pml4, %rax
    movq %rax, kernel_page_table(%rip)

    movb $'D', %cl
    call ser_putc

    /* Call the C kernel (multiboot2 info is in saved_info). */
    call kernel_main

    /* Halt if kernel_main ever returns. */
    cli
3:  hlt
    jmp 3b

/* Send CL to COM1 (for trampoline debugging). */
ser_putc:
    pushq %rax
    pushq %rdx
    movl $0x3F8, %edx
    addl $5, %edx                 /* LSR */
4:  inb %dx, %al
    testb $0x20, %al              /* THR empty? */
    jz 4b
    movl $0x3F8, %edx
    movb %cl, %al
    outb %al, %dx
    popq %rdx
    popq %rax
    ret

/* ----------------------------------------------------------------------------
 * Minimal 64-bit GDT (temporary; replaced by arch/x86_64/cpu.c)
 * ---------------------------------------------------------------------------- */
.section .data.boot, "a"
.align 16
gdt64:
    .quad 0x0000000000000000        /* null */
    .quad 0x00AF9A000000FFFF        /* 64-bit kernel code, DPL0 */
    .quad 0x00CF92000000FFFF        /* kernel data, DPL0 */
gdt64_end:
.align 8
gdt64_ptr:
    .word gdt64_end - gdt64 - 1
    .long gdt64

/* ----------------------------------------------------------------------------
 * Boot data / page tables / stack (low memory)
 * ---------------------------------------------------------------------------- */
.section .data.boot, "a"
.align 4
.global saved_magic
saved_magic: .long 0
.global saved_info
saved_info:  .long 0
/* Physical address of _start_64 (defined by the linker script). */
start64_phys_addr: .long _start_64_phys

.section .bss.boot, "aw", @nobits
.align 4096
.global pml4
pml4:    .skip 4096
pdpt0:   .skip 4096
pd_hi:   .skip 4096
pd0:     .skip 4096
pdpt_hi: .skip 4096
.global boot_pml4
.set boot_pml4, pml4
.align 16
boot_stack_bottom:
    .skip 16384
boot_stack_top:
