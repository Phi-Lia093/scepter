#include "kernel/cpu.h"
#include "kernel/sched.h"
#include "mm/pagefault.h"
#include "driver/char/vga.h"
#include "driver/char/tty.h"
#include "driver/char/pit.h"
#include "driver/char/kbd.h"
#include "driver/char/serial.h"
#include "driver/char/rtc.h"
#include "driver/char/char.h"
#include "driver/pci/pci.h"
#include "driver/block/block.h"
#include "driver/block/ide.h"
#include "driver/block/part_mbr.h"
#include "driver/pic.h"
#include "driver/apic/interrupt.h"
#include "lib/printk.h"
#include "lib/string.h"
#include "mm/mm.h"
#include "mm/slab.h"
#include "driver/block/cache.h"
#include "fs/fs.h"
#include "fs/devfs.h"
#include "fs/minix3.h"
#include "driver/acpi/acpi.h"
#include "kernel/exec.h"


/* =========================================================================
 * kernel_main
 * ========================================================================= */

void kernel_main(void)
{
    /* ------------------------------------------------------------------
     * CPU / interrupt infrastructure (PIC for early boot)
     * ------------------------------------------------------------------ */
    gdt_init();
    tss_init();
    idt_init();
    isr_init();
    
    /* Initialize PIC early for boot (will be replaced by APIC later) */
    pic_init(0x20, 0x28);

    /* VGA and serial for early output */
    vga_init();
    serial_init();  /* Initialize serial port early for logging */

    /* ------------------------------------------------------------------
     * Memory management (detects RAM, initializes buddy & slab)
     * ------------------------------------------------------------------ */
    mm_init();

    /* ------------------------------------------------------------------
     * Kernel services
     * ------------------------------------------------------------------ */
    pagefault_init();
    sched_init();
    cache_init();

    /* ------------------------------------------------------------------
     * Character drivers  (each registers itself + devfs node internally)
     * ------------------------------------------------------------------ */
    tty_init();
    pit_init(100);
    kbd_init();
    rtc_init();  /* RTC prints system time automatically */

    printk("[KERNEL] Early initialization complete\n\n");

    /* ------------------------------------------------------------------
     * ACPI (Advanced Configuration and Power Interface)
     * ------------------------------------------------------------------ */
    acpi_init();           /* Discover RSDP, parse tables, setup shutdown */
    acpi_enum_devices();   /* Enumerate CPUs, I/O APICs, etc. */

    /* ------------------------------------------------------------------
     * Interrupt Controller (APIC or PIC)
     * ------------------------------------------------------------------ */
    interrupt_init();      /* Detect and initialize APIC (or fallback to PIC) */

    /* ------------------------------------------------------------------
     * PCI Bus Enumeration
     * ------------------------------------------------------------------ */
    pci_init();            /* Scan PCI bus and enumerate devices */

    /* ------------------------------------------------------------------
     * Block drivers + partition scan
     * ------------------------------------------------------------------ */
    block_init();

    /* ------------------------------------------------------------------
     * Virtual filesystem
     * ------------------------------------------------------------------ */
    vfs_init();
    devfs_init();   /* mount devfs at /dev */
    minix3_init();  /* register minix3 filesystem driver */

    printk("[KERNEL] Initialization complete\n\n");

    /* ------------------------------------------------------------------
     * Mount root filesystem
     * ------------------------------------------------------------------ */
    if (fs_mount(5, 1, "minix3", "/") != 0) {
        printk("[KERNEL] Failed to mount root filesystem\n");
        sti();
        while (1);
    }
    
    /* ------------------------------------------------------------------
     * Spawn init process
     * ------------------------------------------------------------------ */
    if (spawn_init("/init") < 0) {
        printk("[KERNEL] Failed to spawn init process\n");
        sti();
        while (1);
    }
    
    printk("[KERNEL] Init process spawned, entering scheduler loop\n\n");
    
    /* Enable interrupts and idle - scheduler will switch to init */
    sti();
    
    /* Kernel idle loop - scheduler will preempt us */
    while (1) {
        hlt();
    }
}
