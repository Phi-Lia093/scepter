#include "kernel/panic.h"
#include "arch/cpu.h"
#include "lib/printk.h"
#include <stdint.h>

/* =========================================================================
 * panic – print message, disable interrupts, halt forever
 *
 * The exception entry point (panic_isr + register dump) is implemented
 * by the active arch in arch/<arch>/trap.c.
 * ========================================================================= */

void panic(const char *msg)
{
    printk("\nKERNEL PANIC: %s\n", msg);
    cli();
    for (;;)
        hlt();
}
