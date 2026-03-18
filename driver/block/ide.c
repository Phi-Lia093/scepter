#include "driver/block/ide.h"
#include "driver/block/block.h"
#include "driver/pci/pci.h"
#include "fs/devfs.h"
#include "kernel/asm.h"
#include "lib/printk.h"
#include <stddef.h>

/* =========================================================================
 * Global IDE Disk Array
 * ========================================================================= */

ide_disk_t ide_disks[IDE_MAX_DISKS] = {0};

/* =========================================================================
 * Helper Functions
 * ========================================================================= */

/**
 * Wait for IDE drive to become ready (BSY=0)
 * Returns 0 on success, -1 on timeout
 */
static int ide_wait_bsy(uint16_t base_port)
{
    int timeout = 100000;
    while ((inb(base_port + IDE_REG_STATUS) & IDE_STATUS_BSY) && timeout > 0) {
        timeout--;
    }
    return (timeout > 0) ? 0 : -1;
}

/**
 * Wait for IDE drive to be ready and have data (BSY=0, DRQ=1)
 * Returns 0 on success, -1 on timeout
 */
static int ide_wait_drq(uint16_t base_port)
{
    int timeout = 1000000;
    uint8_t status;

    while (timeout-- > 0) {
        status = inb(base_port + IDE_REG_STATUS);
        if (!(status & IDE_STATUS_BSY) && (status & IDE_STATUS_DRQ)) {
            return 0;
        }
    }
    return -1;
}

/**
 * Select IDE drive (master or slave)
 */
static void ide_select_drive(uint16_t base_port, uint16_t ctrl_port, uint8_t drive)
{
    if (drive == 0)
        outb(base_port + IDE_REG_DRIVE, IDE_DRIVE_MASTER);
    else
        outb(base_port + IDE_REG_DRIVE, IDE_DRIVE_SLAVE);

    /* 400 ns delay via four alt-status reads */
    for (int i = 0; i < 4; i++)
        inb(ctrl_port + IDE_REG_ALTSTATUS);
}

/**
 * Identify IDE disk and fill disk structure.
 * Returns true if disk exists, false otherwise.
 */
static bool ide_identify(uint16_t base_port, uint16_t ctrl_port,
                         uint8_t drive, ide_disk_t *disk)
{
    uint16_t identify_data[256];

    ide_select_drive(base_port, ctrl_port, drive);

    outb(base_port + IDE_REG_SECCOUNT, 0);
    outb(base_port + IDE_REG_LBA_LOW,  0);
    outb(base_port + IDE_REG_LBA_MID,  0);
    outb(base_port + IDE_REG_LBA_HIGH, 0);
    outb(base_port + IDE_REG_COMMAND,  IDE_CMD_IDENTIFY);

    /* Floating bus → no drive */
    uint8_t status = inb(base_port + IDE_REG_STATUS);
    if (status == 0 || status == 0xFF)
        return false;

    if (ide_wait_bsy(base_port) != 0) return false;

    status = inb(base_port + IDE_REG_STATUS);
    if (status & IDE_STATUS_ERR) return false;

    if (ide_wait_drq(base_port) != 0) return false;

    for (int i = 0; i < 256; i++)
        identify_data[i] = inw(base_port + IDE_REG_DATA);

    disk->exists    = true;
    disk->base_port = base_port;
    disk->ctrl_port = ctrl_port;
    disk->drive     = drive;
    disk->sectors   = ((uint32_t)identify_data[61] << 16) | identify_data[60];

    for (int i = 0; i < 20; i++) {
        disk->model[i * 2]     = (char)(identify_data[27 + i] >> 8);
        disk->model[i * 2 + 1] = (char)(identify_data[27 + i] & 0xFF);
    }
    disk->model[40] = '\0';

    for (int i = 39; i >= 0; i--) {
        if (disk->model[i] == ' ') disk->model[i] = '\0';
        else break;
    }

    return true;
}

/* =========================================================================
 * Sector-level I/O
 * ========================================================================= */

int ide_read_sectors(uint8_t disk_id, uint32_t lba, uint8_t count, void *buffer)
{
    if (disk_id >= IDE_MAX_DISKS || !ide_disks[disk_id].exists)
        return -1;

    if (count == 0) count = 1;

    ide_disk_t *disk = &ide_disks[disk_id];
    uint16_t   *buf  = (uint16_t *)buffer;

    /* Wait for not busy */
    int wait_timeout = 1000000;
    while ((inb(disk->base_port + IDE_REG_STATUS) & 0x80) && wait_timeout > 0)
        wait_timeout--;
    if (wait_timeout == 0) return -1;

    /* Select drive + LBA mode */
    uint8_t drive_bits = (disk->drive == 0) ? 0xE0 : 0xF0;
    drive_bits |= ((lba >> 24) & 0x0F);
    outb(disk->base_port + IDE_REG_DRIVE, drive_bits);

    for (int i = 0; i < 1000; i++)
        inb(disk->base_port + IDE_REG_STATUS);

    outb(disk->base_port + IDE_REG_SECCOUNT,  count);
    outb(disk->base_port + IDE_REG_LBA_LOW,  (uint8_t)(lba        & 0xFF));
    outb(disk->base_port + IDE_REG_LBA_MID,  (uint8_t)((lba >> 8) & 0xFF));
    outb(disk->base_port + IDE_REG_LBA_HIGH, (uint8_t)((lba >>16) & 0xFF));
    outb(disk->base_port + IDE_REG_COMMAND,   IDE_CMD_READ_PIO);

    for (uint8_t sector = 0; sector < count; sector++) {
        int timeout = 1000000;
        uint8_t st;
        while (timeout-- > 0) {
            st = inb(disk->base_port + IDE_REG_STATUS);
            if (!(st & 0x80) && (st & 0x08)) break;
        }
        if (timeout <= 0 || !(st & 0x08)) return -1;

        for (int i = 0; i < 256; i++)
            buf[sector * 256 + i] = inw(disk->base_port + IDE_REG_DATA);
    }

    return 0;
}

int ide_write_sectors(uint8_t disk_id, uint32_t lba, uint8_t count,
                      const void *buffer)
{
    if (disk_id >= IDE_MAX_DISKS || !ide_disks[disk_id].exists)
        return -1;

    if (count == 0) count = 1;

    ide_disk_t     *disk = &ide_disks[disk_id];
    const uint16_t *buf  = (const uint16_t *)buffer;

    ide_wait_bsy(disk->base_port);

    uint8_t drive_bits = (disk->drive == 0) ? 0xE0 : 0xF0;
    drive_bits |= ((lba >> 24) & 0x0F);
    outb(disk->base_port + IDE_REG_DRIVE, drive_bits);

    for (int i = 0; i < 1000; i++)
        inb(disk->base_port + IDE_REG_STATUS);

    outb(disk->base_port + IDE_REG_SECCOUNT,  count);
    outb(disk->base_port + IDE_REG_LBA_LOW,  (uint8_t)(lba        & 0xFF));
    outb(disk->base_port + IDE_REG_LBA_MID,  (uint8_t)((lba >> 8) & 0xFF));
    outb(disk->base_port + IDE_REG_LBA_HIGH, (uint8_t)((lba >>16) & 0xFF));
    outb(disk->base_port + IDE_REG_COMMAND,   IDE_CMD_WRITE_PIO);

    for (uint8_t sector = 0; sector < count; sector++) {
        ide_wait_drq(disk->base_port);

        for (int i = 0; i < 256; i++)
            outw(disk->base_port + IDE_REG_DATA, buf[sector * 256 + i]);

        outb(disk->base_port + IDE_REG_COMMAND, 0xE7); /* CACHE FLUSH */
        ide_wait_bsy(disk->base_port);
    }

    return 0;
}

/* =========================================================================
 * Block device callbacks
 * ========================================================================= */

static int ide_block_read(int prim_id, int scnd_id, void *buf,
                          uint32_t offset, size_t count)
{
    (void)scnd_id;
    if (prim_id < 0 || prim_id >= IDE_MAX_DISKS) return -1;
    if (count == 0 || count > 256) return -1;

    if (ide_read_sectors((uint8_t)prim_id, offset, (uint8_t)count, buf) == 0)
        return (int)(count * IDE_SECTOR_SIZE);
    return -1;
}

static int ide_block_write(int prim_id, int scnd_id, const void *buf,
                           uint32_t offset, size_t count)
{
    (void)scnd_id;
    if (prim_id < 0 || prim_id >= IDE_MAX_DISKS) return -1;
    if (count == 0 || count > 256) return -1;

    if (ide_write_sectors((uint8_t)prim_id, offset, (uint8_t)count, buf) == 0)
        return (int)(count * IDE_SECTOR_SIZE);
    return -1;
}

/* =========================================================================
 * Informational helpers
 * ========================================================================= */

void ide_print_disks(void)
{
    static const char *position[] = {
        "Primary Master", "Primary Slave",
        "Secondary Master", "Secondary Slave"
    };

    for (int i = 0; i < IDE_MAX_DISKS; i++) {
        if (ide_disks[i].exists) {
            uint32_t size_mb = ide_disks[i].sectors / 2048;
            printk("[IDE] Disk %d (%s): %s, %u MB (%u sectors)\n",
                   i, position[i], ide_disks[i].model,
                   size_mb, ide_disks[i].sectors);
        }
    }
}

/* =========================================================================
 * PCI IDE Controller Detection
 * ========================================================================= */

static int g_disk_index = 0;  /* Global disk index counter */
static int g_pci_ide_found = 0;

/**
 * Detect disks on an IDE channel
 */
static void ide_detect_channel(uint16_t base_port, uint16_t ctrl_port, const char *name)
{
    if (g_disk_index >= IDE_MAX_DISKS - 1) {
        return;  /* No more slots */
    }
    
    printk("[IDE] Scanning %s channel (base=0x%03x, ctrl=0x%03x)...\n",
           name, base_port, ctrl_port);
    
    /* Try master */
    if (ide_identify(base_port, ctrl_port, 0, &ide_disks[g_disk_index])) {
        printk("[IDE]   Master: %s\n", ide_disks[g_disk_index].model);
        g_disk_index++;
    }
    
    /* Try slave */
    if (g_disk_index < IDE_MAX_DISKS &&
        ide_identify(base_port, ctrl_port, 1, &ide_disks[g_disk_index])) {
        printk("[IDE]   Slave: %s\n", ide_disks[g_disk_index].model);
        g_disk_index++;
    }
}

/**
 * PCI IDE controller callback
 */
static void ide_pci_callback(pci_device_t *pci_dev)
{
    g_pci_ide_found = 1;
    
    printk("[IDE] Found PCI IDE controller: %04x:%04x (prog_if=0x%02x)\n",
           pci_dev->vendor_id, pci_dev->device_id, pci_dev->prog_if);
    
    /* Enable I/O space and bus mastering */
    pci_enable_device(pci_dev->bus, pci_dev->slot, pci_dev->func,
                      PCI_COMMAND_IO | PCI_COMMAND_MASTER);
    
    /* Extract I/O port addresses from BARs
     * BAR0 = Primary Command Block
     * BAR1 = Primary Control Block  
     * BAR2 = Secondary Command Block
     * BAR3 = Secondary Control Block
     * BAR4 = Bus Master IDE */
    
    uint16_t primary_cmd = 0, primary_ctrl = 0;
    uint16_t secondary_cmd = 0, secondary_ctrl = 0;
    
    /* Check if in native PCI mode or compatibility mode */
    if (pci_dev->prog_if & PCI_IDE_PRIMARY_NATIVE) {
        /* Native mode - use BARs */
        if (pci_dev->bar[0] & PCI_BAR_IO) {
            primary_cmd = pci_dev->bar[0] & 0xFFFC;
        }
        if (pci_dev->bar[1] & PCI_BAR_IO) {
            primary_ctrl = pci_dev->bar[1] & 0xFFFC;
        }
    } else {
        /* Compatibility mode - use legacy ports */
        primary_cmd = IDE_PRIMARY_BASE;
        primary_ctrl = IDE_PRIMARY_CTRL;
    }
    
    if (pci_dev->prog_if & PCI_IDE_SECONDARY_NATIVE) {
        /* Native mode - use BARs */
        if (pci_dev->bar[2] & PCI_BAR_IO) {
            secondary_cmd = pci_dev->bar[2] & 0xFFFC;
        }
        if (pci_dev->bar[3] & PCI_BAR_IO) {
            secondary_ctrl = pci_dev->bar[3] & 0xFFFC;
        }
    } else {
        /* Compatibility mode - use legacy ports */
        secondary_cmd = IDE_SECONDARY_BASE;
        secondary_ctrl = IDE_SECONDARY_CTRL;
    }
    
    printk("[IDE]   Primary:   base=0x%03x, ctrl=0x%03x %s\n",
           primary_cmd, primary_ctrl,
           (pci_dev->prog_if & PCI_IDE_PRIMARY_NATIVE) ? "(native)" : "(legacy)");
    printk("[IDE]   Secondary: base=0x%03x, ctrl=0x%03x %s\n",
           secondary_cmd, secondary_ctrl,
           (pci_dev->prog_if & PCI_IDE_SECONDARY_NATIVE) ? "(native)" : "(legacy)");
    
    /* Detect disks on both channels */
    if (primary_cmd && primary_ctrl) {
        ide_detect_channel(primary_cmd, primary_ctrl, "Primary");
    }
    
    if (secondary_cmd && secondary_ctrl) {
        ide_detect_channel(secondary_cmd, secondary_ctrl, "Secondary");
    }
}

/* =========================================================================
 * Initialisation – detect disks + register block devs + devfs nodes
 * ========================================================================= */

void ide_init(void)
{
    printk("[IDE] Initializing IDE driver...\n");
    
    g_disk_index = 0;
    g_pci_ide_found = 0;
    
    /* Try PCI detection first */
    printk("[IDE] Scanning PCI bus for IDE controllers...\n");
    pci_scan_devices(PCI_CLASS_STORAGE, PCI_SUBCLASS_IDE, ide_pci_callback);
    
    /* If no PCI IDE found, try legacy ports */
    if (!g_pci_ide_found) {
        printk("[IDE] No PCI IDE found, trying legacy ports...\n");
        ide_detect_channel(IDE_PRIMARY_BASE, IDE_PRIMARY_CTRL, "Primary");
        ide_detect_channel(IDE_SECONDARY_BASE, IDE_SECONDARY_CTRL, "Secondary");
    }
    
    printk("[IDE] Found %d disk(s)\n", g_disk_index);
    
    /* Register every found disk as a block device + devfs node */
    static const char *names[] = {"hda", "hdb", "hdc", "hdd"};
    block_ops_t ops = { .read = ide_block_read, .write = ide_block_write, .ioctl = NULL };
    
    for (int i = 0; i < g_disk_index && i < IDE_MAX_DISKS; i++) {
        if (!ide_disks[i].exists) continue;
        
        uint32_t size_mb = ide_disks[i].sectors / 2048;
        printk("[IDE] %s: %s, %u MB (%u sectors)\n",
               names[i], ide_disks[i].model, size_mb, ide_disks[i].sectors);
        
        if (register_block_device(i, &ops) == 0) {
            printk("[IDE] Registered %s as block device %d\n", names[i], i);
        } else {
            printk("[IDE] Failed to register %s\n", names[i]);
        }
        devfs_register_device(names[i], DT_BLKDEV, i, 0);
    }
}
