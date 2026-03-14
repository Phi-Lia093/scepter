#include "driver/block/ide.h"
#include "driver/block/block.h"
#include "fs/devfs.h"
#include "asm.h"
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
 * Initialisation – detect disks + register block devs + devfs nodes
 * ========================================================================= */

void ide_init(void)
{
    printk("[IDE] Scanning for disks...\n");

    int disk_count = 0;
    if (ide_identify(IDE_PRIMARY_BASE,   IDE_PRIMARY_CTRL,   0, &ide_disks[0])) disk_count++;
    if (ide_identify(IDE_PRIMARY_BASE,   IDE_PRIMARY_CTRL,   1, &ide_disks[1])) disk_count++;
    if (ide_identify(IDE_SECONDARY_BASE, IDE_SECONDARY_CTRL, 0, &ide_disks[2])) disk_count++;
    if (ide_identify(IDE_SECONDARY_BASE, IDE_SECONDARY_CTRL, 1, &ide_disks[3])) disk_count++;

    printk("[IDE] Found %d disk(s)\n", disk_count);

    ide_print_disks();

    /* Register every found disk as a block device + devfs node */
    static const char *names[] = {"hda", "hdb", "hdc", "hdd"};
    block_ops_t ops = { .read = ide_block_read, .write = ide_block_write, .ioctl = NULL };

    for (int i = 0; i < IDE_MAX_DISKS; i++) {
        if (!ide_disks[i].exists) continue;

        if (register_block_device(i, &ops) == 0) {
            printk("[IDE] Registered %s as block device %d\n", names[i], i);
        } else {
            printk("[IDE] Failed to register %s\n", names[i]);
        }
        devfs_register_device(names[i], DT_BLKDEV, i, 0);
    }
}