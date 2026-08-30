/* ============================================================================
 * Linker Script for Userspace Programs (x86_64)
 *
 * Produces a static non-PIE ELF64 ET_EXEC with two PT_LOAD segments:
 *   - text:  R+X  (.text + .rodata)   FLAGS(5) = PF_R | PF_X
 *   - data:  R+W  (.data + .bss)      FLAGS(6) = PF_R | PF_W  (.bss is NOBITS)
 *
 * Same base address and layout as the i386 script (user space lives in the
 * low 4 GB, x32-style), so the kernel's ELF loader works identically on both
 * architectures.  e_entry == _start == 0x08000000.
 * ============================================================================ */

OUTPUT_FORMAT(elf64-x86-64)
ENTRY(_start)

PHDRS
{
    text PT_LOAD FLAGS(5);
    data PT_LOAD FLAGS(6);
}

SECTIONS
{
    /* Start at userspace base address */
    . = 0x08000000;

    .text : {
        *(.text*)
        *(.rodata*)
    } :text

    .data : {
        *(.data*)
    } :data

    .bss (NOLOAD) : {
        *(.bss*)
        *(COMMON)
    } :data

    /DISCARD/ : {
        *(.interp)
        *(.dynsym)
        *(.dynstr)
        *(.dynamic)
        *(.hash)
        *(.gnu.*)
        *(.note*)
        *(.comment)
        *(.eh_frame*)
    }
}
