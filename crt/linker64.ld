/* ============================================================================
 * Linker Script for Userspace Programs (x86_64)
 *
 * Same flat layout as the i386 version: everything packed into a single
 * section at 0x08000000, entry == load address, so the kernel's flat
 * loader can map the whole file at USER_TEXT_START and jump there.
 * ============================================================================ */

OUTPUT_FORMAT(elf64-x86-64)
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
        *(.gnu.*)
        *(.note*)
        *(.comment)
        *(.eh_frame*)
    }
}
