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
#include "driver/char/random.h"
#include "driver/char/pcspk.h"
#include "driver/char/mouse.h"
#include "kernel/kmsg.h"
#include "driver/pci/pci.h"
#include "driver/block/block.h"
#include "driver/block/ide.h"
#include "driver/block/ahci.h"
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
#include "fs/ext2.h"
#include "fs/procfs.h"
#include "fs/tmpfs.h"
#include "driver/acpi/acpi.h"
#include "driver/video/video.h"
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
    irq_init();   /* install IDT gates for IRQs 0-15 (vectors 32-47) */
    
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
    video_init();  /* switch to VBE graphics + graphics console if available */
    pit_init(100);
    kbd_init();
    mouse_init();  /* PS/2 mouse: /dev/mouse */
    rtc_init();  /* RTC prints system time automatically */
    miscdev_init(); /* /dev/null, /dev/zero */
    random_init();  /* /dev/random, /dev/urandom */
    pcspk_init();   /* /dev/pcspk */
    kmsg_init();    /* /dev/kmsg (kernel log ring buffer) */

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
    devfs_init();   /* register devfs type (init mounts it at /dev) */
    minix3_init();  /* register minix3 filesystem driver */
    ext2_init();    /* register ext2 filesystem driver */
    procfs_init();  /* register procfs type (init mounts it at /proc) */
    tmpfs_init();   /* register tmpfs type (init mounts it at /tmp) */

    printk("[KERNEL] Initialization complete (v2-syscalls)\n\n");

    /* ------------------------------------------------------------------
     * Mount root filesystem
     * ------------------------------------------------------------------ */
    /* The merged boot+root disk is the first IDE disk by default:
     * hda1 = block device 4, partition 1.  The root filesystem is ext2.
     * If ext2 mounting fails (e.g. a stale disk), fall back to minix3. */
    int root_dev = (ahci_disk_count() > 0) ? 12 : 4;
    if (fs_mount(root_dev, 1, "ext2", "/") != 0) {
        printk("[KERNEL] ext2 root mount failed (dev %d), trying minix3...\n",
               root_dev);
        if (fs_mount(root_dev, 1, "minix3", "/") != 0) {
            printk("[KERNEL] Failed to mount root filesystem (dev %d)\n",
                   root_dev);
            sti();
            while (1);
        }
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
    
    /* Kernel idle loop - the scheduler preempts us when a user task is
     * runnable; otherwise we halt here (true idle task).
     *
     * schedule() after hlt is essential for I/O latency: blocked tasks
     * (e.g. an AHCI DMA read waiting in sleep_on) are woken by their
     * completion IRQ while we sit in sti;hlt.  Without this, a woken
     * task would stay READY until the PIT's periodic schedule() - which
     * only fires every 10 ticks (100 ms) - turning every 512-byte sector
     * read into a ~100 ms round trip. */
    for (;;) {
        sti();
        hlt();
        schedule();
    }
}
