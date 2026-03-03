/* -----------------------------------------------------------------------
 * Multiboot Header
 * Required for GRUB and QEMU -kernel to load the kernel
 * Magic: 0x1BADB002, Flags: 0x00000003 (ALIGN + MEMINFO)
 * Checksum: -(0x1BADB002 + 0x00000003) = 0xE4524FFB
 * ----------------------------------------------------------------------- */
.section .multiboot
.align 4

multiboot_header:
    .long 0x1BADB002        /* Magic */
    .long 0x00000003        /* Flags: ALIGN + MEMINFO */
    .long 0xE4524FFB        /* Checksum */

/* -----------------------------------------------------------------------
 * Boot entry – runs at physical address (paging not yet enabled)
 * ----------------------------------------------------------------------- */
.section .text
.global _start
_start:
    /* Simple entry point - no bootloader dependencies */

    /* ================================================================
     * Build page directory and page tables
     * ================================================================ */

    /* ---- Zero page directory ---- */
    /* Convert virtual address to physical by subtracting KERNEL_VMA */
    movl  $boot_page_directory, %edi
    subl  $0xC0000000, %edi
    movl  $1024, %ecx
    xorl  %eax, %eax
    rep   stosl

    /* ---- Zero all 256 page tables ---- */
    movl  $boot_page_tables, %edi
    subl  $0xC0000000, %edi
    movl  $262144, %ecx           /* 256 tables × 1024 entries */
    xorl  %eax, %eax
    rep   stosl

    /* ---- Setup identity mapping: PDE[0] → first page table ---- */
    /* This is temporary, needed while EIP is still in low memory */
    movl  $boot_page_tables, %eax
    subl  $0xC0000000, %eax       /* convert to physical */
    orl   $0x03, %eax             /* Present | Writable */
    movl  $boot_page_directory, %edi
    subl  $0xC0000000, %edi
    movl  %eax, (%edi)

    /* Fill first page table: map phys 0x00000000 - 0x003FFFFF (4 MB) */
    movl  $boot_page_tables, %edi
    subl  $0xC0000000, %edi
    movl  $0x00000003, %eax       /* phys 0x00000000 | Present | Writable */
    movl  $1024, %ecx
1:
    movl  %eax, (%edi)
    addl  $0x1000, %eax           /* next 4 KB page */
    addl  $4, %edi                /* next PTE */
    loop  1b

    /* ---- Setup higher-half mapping: PDE[768..1023] → page tables ---- */
    /* Map physical 0x00000000 - 0x3FFFFFFF to virtual 0xC0000000 - 0xFFFFFFFF */

    movl  $boot_page_directory, %edi
    subl  $0xC0000000, %edi       /* convert to physical */
    addl  $3072, %edi             /* PDE[768] offset = 768 × 4 = 0xC00 */
    
    movl  $boot_page_tables, %eax /* start with first page table */
    subl  $0xC0000000, %eax       /* convert to physical */
    movl  $256, %ecx              /* 256 page tables for 1 GB */

2:
    movl  %eax, %edx
    orl   $0x03, %edx             /* Present | Writable */
    movl  %edx, (%edi)
    addl  $4096, %eax             /* next page table (4 KB) */
    addl  $4, %edi                /* next PDE */
    loop  2b

    /* ---- Fill all 256 page tables with PTEs ---- */
    /* Each page table maps 4 MB (1024 pages × 4 KB) */
    
    movl  $boot_page_tables, %edi
    subl  $0xC0000000, %edi       /* convert to physical */
    movl  $0x00000003, %eax       /* start at phys 0x00000000 | Present | Writable */
    movl  $262144, %ecx           /* 256 tables × 1024 entries */

3:
    movl  %eax, (%edi)
    addl  $0x1000, %eax           /* next 4 KB page */
    addl  $4, %edi                /* next PTE */
    loop  3b

    /* ================================================================
     * Enable paging
     * ================================================================ */

    /* Load CR3 with page directory physical address */
    movl  $boot_page_directory, %eax
    subl  $0xC0000000, %eax       /* convert to physical */
    movl  %eax, %cr3

    /* Enable paging in CR0 */
    movl  %cr0, %eax
    orl   $0x80000000, %eax
    movl  %eax, %cr0

    /* Jump to higher-half code (CS will be reset by kernel's gdt_init) */
    subl %eax, %eax
    movw %cs, %ax
    pushl %eax
    pushl $_start_higher
    retf

/* -----------------------------------------------------------------------
 * Now running at virtual 0xC0xxxxxx
 * ----------------------------------------------------------------------- */
_start_higher:
    /* Remove identity mapping (PDE[0]) - now we can use virtual address */
    movl  $0, (boot_page_directory)

    /* Flush TLB by reloading CR3 */
    movl  %cr3, %eax
    movl  %eax, %cr3

    /* Set up kernel stack (virtual address) */
    movl  $stack_top, %esp

    mov $boot_page_directory-0xC0000000, kernel_page_table 

    /* Call the C kernel (no arguments needed) */
    call  kernel_main

    /* Halt if kernel_main ever returns */
    cli
1:  hlt
    jmp   1b

/* -----------------------------------------------------------------------
 * Page tables in .bss (statically allocated)
 *
 * We pre-map 1 GB of physical memory (0x00000000 - 0x3FFFFFFF)
 * to virtual addresses 0xC0000000 - 0xFFFFFFFF (higher-half).
 *
 * Memory layout:
 *   - 1 page directory  (4 KB)  - 1024 PDEs
 *   - 256 page tables   (1 MB)  - each PT has 1024 PTEs, covers 4 MB
 *   Total: 256 PTs × 4 MB = 1024 MB = 1 GB
 * ----------------------------------------------------------------------- */
.section .data
.align 4096
boot_page_directory:
    .skip 4096                    /* 1024 PDEs × 4 bytes */

.align 4096
boot_page_tables:
    .skip 1048576                 /* 256 page tables × 4096 bytes */

/* -----------------------------------------------------------------------
 * 16 KB kernel stack
 * ----------------------------------------------------------------------- */
.align 16
stack_bottom:
    .skip 16384
stack_top: