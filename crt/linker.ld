/* ============================================================================
 * Linker Script for Userspace Programs (i386)
 *
 * Produces a static non-PIE ELF32 ET_EXEC with two PT_LOAD segments:
 *   - text:  R+X  (.text + .rodata)   FLAGS(5) = PF_R | PF_X
 *   - data:  R+W  (.data + .bss)      FLAGS(6) = PF_R | PF_W  (.bss is NOBITS)
 *
 * The kernel's ELF loader maps each segment with matching permissions,
 * zero-fills the .bss tail (p_filesz < p_memsz), and jumps to e_entry
 * (_start, at the base address 0x08000000).
 *
 * A legacy flat build (objcopy -O binary) is still possible via
 * 'make -C crt flat'; the kernel keeps flat binary support as a fallback.
 * ============================================================================ */

OUTPUT_FORMAT(elf32-i386)
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
        *(.gnu.hash)
        *(.rel.dyn)
        *(.rel.plt)
        *(.plt*)
        *(.got*)
        *(.note*)
        *(.comment*)
        *(.eh_frame*)
    }
}
