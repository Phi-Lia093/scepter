#include "kernel/cpu.h"
#include "kernel/sched.h"
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
 * Test userspace execution
 * ========================================================================= */

static void test_userspace_exec(void)
{
    printk("\n========================================\n");
    printk("  USERSPACE EXECUTION TEST\n");
    printk("========================================\n\n");
    
    printk("[TEST] Attempting to execute /test.bin\n");
    printk("[TEST] This should:\n");
    printk("[TEST]   1. Load flat binary\n");
    printk("[TEST]   2. Create user page directory (no kernel mappings)\n");
    printk("[TEST]   3. Map binary at 0x08000000 with RWX\n");
    printk("[TEST]   4. Switch CR3 to user page directory\n");
    printk("[TEST]   5. IRET to userspace\n");
    printk("[TEST]   6. Execute: mov eax, 0xDEADBEEF; jmp $\n");
    printk("[TEST] Expected: Infinite loop in userspace\n");
    printk("[TEST] Use (qemu) info registers to verify EAX=0xDEADBEEF\n\n");
    
    /* This call does NOT return! */
    exec_flat("/test.bin");
    
    /* Should never reach here */
    printk("[TEST] ERROR: exec_flat returned!\n");
}

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
     * Test userspace execution
     * ------------------------------------------------------------------ */
    test_userspace_exec();  /* Does NOT return! */
    
    /* Should never reach here */
    printk("[KERNEL] ERROR: Returned from test!\n");
    sti();
    while (1);
}
