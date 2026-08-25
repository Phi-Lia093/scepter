/* ============================================================================
 * Linker Script for Userspace Programs
 *
 * Everything (text, rodata, data, bss) is packed into a single section at
 * 0x08000000.  The build links a static non-PIE ELF and then converts it
 * with objcopy -O binary, which EMITS the .bss region as zero bytes so that
 * static/global variables are actually present in the flat file the kernel
 * loads (and therefore mapped into the process address space).
 * ============================================================================ */

ENTRY(_start)

SECTIONS
{
    /* Start at userspace base address */
    . = 0x08000000;

    .text : {
        *(.text*)
        *(.rodata*)
        *(.data*)
        *(.bss*)
    }

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
