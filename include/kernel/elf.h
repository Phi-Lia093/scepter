#ifndef KERNEL_ELF_H
#define KERNEL_ELF_H

#include <stdint.h>

/* ============================================================================
 * Minimal ELF definitions (self-contained)
 *
 * The kernel is freestanding, so it cannot rely on the host <elf.h>.  Only
 * the subset needed by the executable loader is declared: the 32- and 64-bit
 * file headers, program headers, and the constants the loader keys off
 * (ident indices, class/data/version, ET_EXEC, PT_LOAD, PF_*).
 * ============================================================================ */

/* e_ident[] indices */
#define EI_MAG0        0
#define EI_MAG1        1
#define EI_MAG2        2
#define EI_MAG3        3
#define EI_CLASS       4
#define EI_DATA        5
#define EI_VERSION     6
#define EI_NIDENT      16

#define ELFMAG0        0x7f
#define ELFMAG1        'E'
#define ELFMAG2        'L'
#define ELFMAG3        'F'

#define ELFCLASS32     1
#define ELFCLASS64     2

#define ELFDATA2LSB    1
#define ELFDATA2MSB    2

#define EV_CURRENT     1

/* e_type */
#define ET_EXEC        2

/* p_type */
#define PT_LOAD        1

/* p_flags */
#define PF_X           1
#define PF_W           2
#define PF_R           4

/* Native ELF class, derived from the native pointer width. */
#if UINTPTR_MAX == 0xFFFFFFFFU
#define NATIVE_ELF_CLASS ELFCLASS32
#elif UINTPTR_MAX == 0xFFFFFFFFFFFFFFFFULL
#define NATIVE_ELF_CLASS ELFCLASS64
#else
#error "Unsupported native word size for the ELF loader"
#endif

/* ----------------------------------------------------------------------------
 * ELF32
 * -------------------------------------------------------------------------- */

typedef struct {
    uint8_t  e_ident[EI_NIDENT];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint32_t e_entry;
    uint32_t e_phoff;
    uint32_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} Elf32_Ehdr;

typedef struct {
    uint32_t p_type;
    uint32_t p_offset;
    uint32_t p_vaddr;
    uint32_t p_paddr;
    uint32_t p_filesz;
    uint32_t p_memsz;
    uint32_t p_flags;
    uint32_t p_align;
} Elf32_Phdr;

/* ----------------------------------------------------------------------------
 * ELF64
 * -------------------------------------------------------------------------- */

typedef struct {
    uint8_t  e_ident[EI_NIDENT];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} Elf64_Ehdr;

typedef struct {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} Elf64_Phdr;

#endif /* KERNEL_ELF_H */
