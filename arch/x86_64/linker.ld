/* ============================================================================
 * x86_64 kernel linker script
 *
 * Two load regions:
 *   - a low trampoline (.boot at physical 1 MB, identity-mapped) that
 *     contains the multiboot2 header, the 32-bit long-mode setup code,
 *     the boot page tables and the boot stack;
 *   - the higher-half kernel (VMA 0xFFFFFFFF80000000) loaded at 2 MB.
 * ============================================================================ */

OUTPUT_FORMAT(elf64-x86-64)
ENTRY(_start)

KERNEL_VMA       = 0xFFFFFFFF80000000;  /* direct map: phys X -> KERNEL_VMA+X */
KERNEL_PHYS_BASE = 0x200000;            /* physical load address of the kernel */

SECTIONS
{
    /* ---------------- Low trampoline (identity mapped) ---------------- */
    . = KERNEL_PHYS_BASE - 0x100000;   /* 1 MB */

    .boot : {
        *(.multiboot2)
        *(.text.boot)
        *(.data.boot)
        . = ALIGN(4096);
        *(.bss.boot)
        . = ALIGN(4096);
    }

    /* ---------------- Higher-half kernel ---------------- */
    . = KERNEL_VMA + KERNEL_PHYS_BASE;

    .text : AT(ADDR(.text) - KERNEL_VMA) {
        *(.text*)
    }
    . = ALIGN(4096);
    .rodata : AT(ADDR(.rodata) - KERNEL_VMA) {
        *(.rodata*)
    }
    . = ALIGN(4096);
    .data : AT(ADDR(.data) - KERNEL_VMA) {
        *(.data*)
    }
    . = ALIGN(4096);
    .bss : AT(ADDR(.bss) - KERNEL_VMA) {
        _bss_start_v = .;
        *(.bss*)
        *(COMMON*)
    }
    . = ALIGN(4096);
    kernel_end = .;

    /* Physical (identity) address of the 64-bit entry, for the trampoline. */
    _start_64_phys = _start_64 - KERNEL_VMA;

    /DISCARD/ : {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
    }
}
