/* ============================================================================
 * Physical Memory Detection (i386 CMOS)
 * ============================================================================ */

#include "arch/mem.h"
#include "arch/io.h"

uint32_t arch_mem_detect_kb(void)
{
    /* Extended memory (1 MB – 16 MB) from CMOS registers 0x17-0x18 */
    outb(0x70, 0x17);
    uint32_t low = inb(0x71);
    outb(0x70, 0x18);
    uint32_t high = inb(0x71);
    uint32_t extended_kb = (high << 8) | low;

    /* Memory above 16 MB from CMOS registers 0x34-0x35 (64 KB blocks) */
    outb(0x70, 0x34);
    low = inb(0x71);
    outb(0x70, 0x35);
    high = inb(0x71);
    uint32_t above_16mb_kb = ((high << 8) | low) * 64;

    return 1024 + extended_kb + above_16mb_kb;
}
