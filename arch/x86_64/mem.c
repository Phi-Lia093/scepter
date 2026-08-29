/* ============================================================================
 * Physical memory detection (x86_64) – from the multiboot2 memory map.
 * ============================================================================ */

#include "arch/mem.h"
#include "arch/paging.h"
#include "lib/printk.h"
#include <stdint.h>

/* Saved by boot.s: multiboot2 magic + boot info pointer (physical). */
extern uint32_t saved_magic;
extern uint32_t saved_info;

/* Minimal multiboot2 boot-info / tag structures. */
struct mb2_tag {
    uint32_t type;
    uint32_t size;
};

struct mb2_mmap_entry {
    uint64_t addr;
    uint64_t len;
    uint32_t type;         /* 1 = usable RAM */
    uint32_t reserved;
};

struct mb2_tag_mmap {
    uint32_t type;
    uint32_t size;
    uint32_t entry_size;
    uint32_t entry_version;
    struct mb2_mmap_entry entries[];
};

#define MB2_MMAP_TYPE  6
#define MB2_MEM_USABLE 1

uint32_t arch_mem_detect_kb(void)
{
    if (saved_magic != 0x36D76289) {   /* MULTIBOOT2_BOOTLOADER_MAGIC */
        /* No multiboot2 info (e.g. direct -kernel boot): fall back to a
         * fixed 128 MB so the system can still come up. */
        printk("[MEM] No multiboot2 info (magic=0x%x), assuming 128 MB\n",
               saved_magic);
        return 128 * 1024;
    }

    /* The boot info pointer is a physical address; read via the direct map. */
    uint8_t *mbi = (uint8_t *)PHYS_TO_VIRT((uintptr_t)saved_info);
    uint32_t total_size = *(uint32_t *)mbi;

    uint64_t total_bytes = 0;
    uintptr_t off = 8;   /* skip total_size + reserved */

    while (off + 8 <= total_size) {
        struct mb2_tag *tag = (struct mb2_tag *)(mbi + off);
        if (tag->type == 0)
            break;                       /* end tag */
        if (tag->type == MB2_MMAP_TYPE && tag->size >= 16) {
            struct mb2_tag_mmap *mm = (struct mb2_tag_mmap *)tag;
            uint32_t esz = mm->entry_size ? mm->entry_size : 24;
            uint32_t n = (tag->size - 16) / esz;
            for (uint32_t i = 0; i < n; i++) {
                struct mb2_mmap_entry *en =
                    (struct mb2_mmap_entry *)((uint8_t *)mm->entries + i * esz);
                if (en->type == MB2_MEM_USABLE)
                    total_bytes += en->len;
            }
        }
        off += (tag->size + 7) & ~7;     /* tags are 8-byte aligned */
    }

    return (uint32_t)(total_bytes / 1024);
}
