#include "driver/block/ahci.h"
#include "driver/block/block.h"
#include "driver/block/part_mbr.h"
#include "driver/pci/pci.h"
#include "arch/irq.h"
#include "arch/timer.h"
#include "fs/devfs.h"
#include "arch/cpu.h"
#include "kernel/sched.h"
#include "mm/buddy.h"
#include "mm/mm.h"
#include "mm/vmalloc.h"
#include "lib/string.h"
#include "lib/printk.h"
#include <stddef.h>

/* =========================================================================
 * Global AHCI State
 * ========================================================================= */

typedef struct {
    volatile uint32_t *regs;        /* ioremapped ABAR */
    uint32_t abar_phys;
    uint32_t cap;
    uint32_t version;
    uint8_t  irq;
} ahci_hba_t;

typedef struct {
    bool     present;
    int      index;                 /* physical port index */
    int      active_slot;           /* command slot in flight, -1 = idle */
    volatile int done;              /* set by ISR when command completes */
    volatile int error;             /* set by ISR on TFES */

    uint32_t    clb_phys;           /* command list (1 KB, page aligned) */
    void       *clb_virt;
    uint32_t    fb_phys;            /* FIS receive area (256 B) */
    void       *fb_virt;
    uint32_t    ct_phys[AHCI_MAX_SLOTS];   /* command tables (256 B) */
    void       *ct_virt[AHCI_MAX_SLOTS];
    uint32_t    bounce_phys;        /* 128 KB DMA bounce buffer */
    void       *bounce_virt;

    wait_queue_head_t wq;           /* waiters blocked on command completion */

    uint32_t sectors;
    int      lba48;
    char     model[41];
} ahci_port_t;

static ahci_hba_t  g_hba;
static ahci_port_t g_ports[AHCI_MAX_PORTS];
static int         g_port_count = 0;   /* number of present disks */
static int         g_ahci_found = 0;
static int         g_use_irq = 0;      /* 0 = poll fallback */

/* =========================================================================
 * Register Access Helpers
 * ========================================================================= */

static inline uint32_t hba_read(uint32_t off)
{
    return g_hba.regs[off / 4];
}

static inline void hba_write(uint32_t off, uint32_t val)
{
    g_hba.regs[off / 4] = val;
}

static inline uint32_t port_read(ahci_port_t *p, uint32_t off)
{
    return g_hba.regs[(AHCI_PORT_BASE + p->index * AHCI_PORT_STRIDE + off) / 4];
}

static inline void port_write(ahci_port_t *p, uint32_t off, uint32_t val)
{
    g_hba.regs[(AHCI_PORT_BASE + p->index * AHCI_PORT_STRIDE + off) / 4] = val;
}

/* =========================================================================
 * Helpers
 * ========================================================================= */

/* Compiler + CPU ordering barrier for MMIO/DMA ordering. */
static inline void ahci_mb(void)
{
    __asm__ volatile("lock; addl $0, (%%esp)" ::: "memory");
}

/* =========================================================================
 * Timing helpers
 *
 * Boot-time waits run with interrupts disabled, so they use the TSC
 * calibrated once against one full PIT channel-0 countdown period (~10 ms
 * at the 100 Hz setting programmed by arch_timer_init()).  The timer counter is
 * free-running and only needs to be read, so no IRQs are required.
 * All arithmetic is 32-bit friendly (no __udivdi3 / libgcc dependency).
 * ========================================================================= */

static uint32_t g_tsc_per_ms = 1000000;   /* fallback: assume ~1 GHz */

static inline uint64_t ahci_rdtsc(void)
{
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

/* Latch and read the free-running timer counter (via the arch timer API). */
static inline uint16_t ahci_pit_count(void)
{
    return arch_timer_read_count();
}

/* Measure TSC ticks per millisecond from one PIT countdown period. */
static void ahci_calibrate_clock(void)
{
    uint16_t last, cur;
    uint32_t dt;
    uint64_t t0, t1;
    int i;

    last = ahci_pit_count();
    t0   = ahci_rdtsc();
    for (i = 0; i < 100000000; i++) {
        cur = ahci_pit_count();
        /* A wrap (counter reloading to a much larger value) marks one
         * full period.  A normal read-to-read delta is tiny. */
        if ((uint16_t)(cur - last) > 0x2000) {
            t1 = ahci_rdtsc();
            dt = (uint32_t)(t1 - t0);           /* ~10 ms, fits in 32 bits */
            g_tsc_per_ms = dt / 10;
            return;
        }
        last = cur;
    }
    g_tsc_per_ms = 1000000;                   /* PIT not running */
}

/* Busy-wait for ms milliseconds (TSC based). */
static void ahci_mdelay(uint32_t ms)
{
    uint64_t target = (uint64_t)ms * g_tsc_per_ms;
    uint64_t start  = ahci_rdtsc();
    while (ahci_rdtsc() - start < target)
        __asm__ volatile("" ::: "memory");
}

/* Monotonic deadline helper: returns now + limit_ms in TSC ticks. */
static inline uint64_t ahci_deadline(uint32_t limit_ms)
{
    return ahci_rdtsc() + (uint64_t)limit_ms * g_tsc_per_ms;
}

/* =========================================================================
 * Command Engine
 *
 * Submits one command in slot 0 of the port's command list.  The data
 * region is a single physically-contiguous PRD (the per-port bounce
 * buffer).  Two completion modes:
 *
 *   wait_irq = 0  poll PxCI/PxIS   (boot-time identify, IF=0, no other task)
 *   wait_irq = 1  mask → issue → unmask → sleep_on(port->wq); the ISR
 *                 wakes us when the device clears PxCI.  The caller must
 *                 hold interrupts disabled (irq_save) while waiting.
 * ========================================================================= */

static int ahci_command(ahci_port_t *p, ahci_fis_h2d_t *fis,
                        uint32_t data_phys, uint32_t data_len,
                        int write, int wait_irq)
{
    int slot = 0;
    ahci_cmd_hdr_t *hdr = (ahci_cmd_hdr_t *)p->clb_virt + slot;
    ahci_cmd_table_t *ct = (ahci_cmd_table_t *)p->ct_virt[slot];

    /* Command header: 5-dword H2D FIS, single PRD. */
    memset(hdr, 0, sizeof(ahci_cmd_hdr_t));
    hdr->cfl   = 5;
    hdr->w     = write ? 1 : 0;
    hdr->prdtl = (data_len > 0) ? 1 : 0;
    hdr->ctba  = p->ct_phys[slot];
    hdr->ctbau = 0;

    /* Command table: FIS followed by the PRD. */
    memset(ct, 0, sizeof(ahci_cmd_table_t));
    memcpy(ct->cfis, fis, sizeof(ahci_fis_h2d_t));
    if (data_len > 0) {
        ct->prdt[0].dba  = data_phys;
        ct->prdt[0].dbau = 0;
        ct->prdt[0].dbc  = (data_len - 1) | (1u << 31);   /* I bit */
    }
    ahci_mb();

    p->done = 0;
    p->error = 0;
    p->active_slot = slot;

    /* Clear stale status, then issue. */
    port_write(p, AHCI_PORT_IS, 0xFFFFFFFF);
    port_write(p, AHCI_PORT_CI, 1u << slot);

    if (wait_irq) {
        /* Unmask the completion interrupt.  The wait loop also checks
         * PxCI directly so a command that finished before unmasking (or a
         * missed interrupt) still completes without hanging. */
        port_write(p, AHCI_PORT_IE, AHCI_IE_COMPLETION);
        while (!p->done) {
            if (!(port_read(p, AHCI_PORT_CI) & (1u << slot))) {
                p->done = 1;
                break;
            }
            sleep_on(&p->wq);
        }
        port_write(p, AHCI_PORT_IE, 0);
        return p->error ? -1 : 0;
    }

    /* Poll mode: spin until the HBA clears the slot (time bounded). */
    {
        uint64_t deadline = ahci_deadline(AHCI_TIMEOUT_CMD_MS);
        while (ahci_rdtsc() < deadline) {
            if (!(port_read(p, AHCI_PORT_CI) & (1u << slot)))
                break;
            ahci_mdelay(1);
        }
        if (port_read(p, AHCI_PORT_CI) & (1u << slot)) {
            printk("[AHCI] Port %d: command timeout (CI=0x%08x)\n",
                   p->index, port_read(p, AHCI_PORT_CI));
            return -1;
        }
    }

    if (port_read(p, AHCI_PORT_IS) & AHCI_IS_TFES) {
        printk("[AHCI] Port %d: task file error (TFD=0x%08x SERR=0x%08x)\n",
               p->index, port_read(p, AHCI_PORT_TFD),
               port_read(p, AHCI_PORT_SERR));
        port_write(p, AHCI_PORT_IS, 0xFFFFFFFF);
        return -1;
    }
    port_write(p, AHCI_PORT_IS, 0xFFFFFFFF);
    return 0;
}

/* =========================================================================
 * Port Bring-up
 * ========================================================================= */

static int ahci_port_start(ahci_port_t *p)
{
    uint32_t cmd;

    /* 1. Stop the port if it is already running. */
    cmd = port_read(p, AHCI_PORT_CMD);
    if (cmd & (AHCI_CMD_ST | AHCI_CMD_FRE)) {
        uint64_t deadline = ahci_deadline(AHCI_TIMEOUT_STOP_MS);

        port_write(p, AHCI_PORT_CMD, cmd & ~(AHCI_CMD_ST | AHCI_CMD_FRE));
        while ((port_read(p, AHCI_PORT_CMD) & (AHCI_CMD_FR | AHCI_CMD_CR)) &&
               ahci_rdtsc() < deadline)
            ahci_mdelay(1);
    }

    /* 2. Point the HBA at our command list and FIS receive area. */
    port_write(p, AHCI_PORT_CLB,  p->clb_phys);
    port_write(p, AHCI_PORT_CLBU, 0);
    port_write(p, AHCI_PORT_FB,   p->fb_phys);
    port_write(p, AHCI_PORT_FBU,  0);

    /* 3. Clear error and interrupt status. */
    port_write(p, AHCI_PORT_SERR, 0xFFFFFFFF);
    port_write(p, AHCI_PORT_IS,   0xFFFFFFFF);

    /* 4. Enable FIS receive, spin-up, power-on and start. */
    port_write(p, AHCI_PORT_CMD,
               AHCI_CMD_FRE | AHCI_CMD_SUD | AHCI_CMD_POD | AHCI_CMD_ST);

    /* 5. Wait for the FIS receive engine to start (time bounded). */
    {
        uint64_t deadline = ahci_deadline(AHCI_TIMEOUT_FRE_MS);

        while (!(port_read(p, AHCI_PORT_CMD) & AHCI_CMD_FR) &&
               ahci_rdtsc() < deadline)
            ahci_mdelay(1);
        if (!(port_read(p, AHCI_PORT_CMD) & AHCI_CMD_FR)) {
            printk("[AHCI] Port %d: FIS receive engine did not start\n",
                   p->index);
            return -1;
        }
    }

    /* 6. Wait for the SATA PHY to go online (DET=3, IPM=1).
     *
     * Two windows:
     *   - detect window: wait for a device to be detected (DET=1/2).  A
     *     port that still shows no device (DET=0) or a PHY error (DET=4)
     *     after this is empty and is skipped immediately.
     *   - link window: once a device is detected, wait for the PHY to
     *     finish negotiating (DET=3).
     * This is what makes scanning a controller with several unused ports
     * fast: empty ports cost only AHCI_TIMEOUT_DETECT_MS each. */
    {
        uint64_t start = ahci_rdtsc();
        uint64_t detect_deadline = start +
                                   (uint64_t)AHCI_TIMEOUT_DETECT_MS *
                                   g_tsc_per_ms;
        uint64_t link_deadline = start +
                                 (uint64_t)AHCI_TIMEOUT_LINK_MS *
                                 g_tsc_per_ms;
        int detected = 0;

        while (ahci_rdtsc() < link_deadline) {
            uint32_t ssts = port_read(p, AHCI_PORT_SSTS);
            uint32_t det = ssts & AHCI_SSTS_DET;

            if (det == AHCI_SSTS_DET_ONLINE &&
                (ssts & AHCI_SSTS_IPM) == AHCI_SSTS_IPM_ACTIVE)
                return 0;
            if (det == AHCI_SSTS_DET_PRESENT ||
                det == (uint32_t)0x02)         /* device detected */
                detected = 1;
            if (det == AHCI_SSTS_DET_ERR)      /* PHY error: give up */
                break;
            if (!detected && ahci_rdtsc() >= detect_deadline)
                break;                         /* no device: empty port */
            ahci_mdelay(1);
        }
        return -1;
    }
}

/* =========================================================================
 * Identify
 * ========================================================================= */

static int ahci_identify(ahci_port_t *p)
{
    ahci_fis_h2d_t fis;

    memset(&fis, 0, sizeof(fis));
    fis.fis_type = AHCI_FIS_H2D;
    fis.pmport_c = 0x80;                 /* C=1 */
    fis.command  = ATA_CMD_IDENTIFY;
    fis.device   = 0x40;
    fis.countl   = 1;

    if (ahci_command(p, &fis, p->bounce_phys, AHCI_SECTOR_SIZE, 0, 0) != 0)
        return -1;

    uint16_t *id = (uint16_t *)p->bounce_virt;
    if (id[0] == 0x0000 || id[0] == 0xFFFF)
        return -1;

    /* Sector counts: LBA28 (words 60-61), LBA48 (words 100-103). */
    uint32_t lba28 = ((uint32_t)id[61] << 16) | id[60];
    p->lba48 = (id[83] & (1 << 10)) ? 1 : 0;
    p->sectors = lba28;
    if (p->lba48) {
        uint64_t lba48_count = ((uint64_t)id[101] << 16) | id[100];
        if (lba48_count > 0 && lba48_count <= 0xFFFFFFFFull)
            p->sectors = (uint32_t)lba48_count;
    }

    /* Model string: words 27-46, big-endian bytes. */
    for (int i = 0; i < 20; i++) {
        p->model[i * 2]     = (char)(id[27 + i] >> 8);
        p->model[i * 2 + 1] = (char)(id[27 + i] & 0xFF);
    }
    p->model[40] = '\0';
    for (int i = 39; i >= 0; i--) {
        if (p->model[i] == ' ') p->model[i] = '\0';
        else break;
    }
    return 0;
}

/* =========================================================================
 * Interrupt Handler
 *
 * The port completion interrupt for a DMA command arrives as a D2H
 * register FIS.  We only wake the waiter once the HBA has cleared PxCI
 * for the in-flight slot, so spurious port interrupts (PCS/CPDS) can
 * never complete a command early.
 * ========================================================================= */

void ahci_isr(uint32_t cs)
{
    uint32_t is;

    (void)cs;
    if (!g_hba.regs)
        return;

    is = hba_read(AHCI_HBA_IS);
    if (!is)
        return;

    for (int i = 0; i < g_port_count; i++) {
        ahci_port_t *p = &g_ports[i];

        if (!(is & (1u << p->index)))
            continue;

        if (p->active_slot >= 0 &&
            !(port_read(p, AHCI_PORT_CI) & (1u << p->active_slot))) {
            if (port_read(p, AHCI_PORT_IS) & AHCI_IS_TFES)
                p->error = 1;
            port_write(p, AHCI_PORT_IE, 0);        /* mask port */
            p->done = 1;
            wake_up(&p->wq);
        }
        port_write(p, AHCI_PORT_IS, 0xFFFFFFFF);   /* clear port status */
    }

    hba_write(AHCI_HBA_IS, is);                    /* clear HBA status */
}

/* =========================================================================
 * Sector-level I/O (DMA + interrupt completion)
 * ========================================================================= */

static int ahci_dma_rw(ahci_port_t *p, uint32_t lba, uint32_t count,
                       void *buf, int write)
{
    uint32_t done = 0;

    while (done < count) {
        uint32_t chunk = count - done;
        ahci_fis_h2d_t fis;

        if (chunk > AHCI_MAX_SECTORS)
            chunk = AHCI_MAX_SECTORS;

        if (write)
            memcpy(p->bounce_virt,
                   (uint8_t *)buf + done * AHCI_SECTOR_SIZE,
                   chunk * AHCI_SECTOR_SIZE);

        memset(&fis, 0, sizeof(fis));
        fis.fis_type = AHCI_FIS_H2D;
        fis.pmport_c = 0x80;
        fis.device   = 0x40;

        if (p->lba48) {
            uint64_t lba48 = (uint64_t)lba;
            fis.command = write ? ATA_CMD_WRITE_DMA_EXT
                                : ATA_CMD_READ_DMA_EXT;
            fis.lba0 = (uint8_t)(lba48 & 0xFF);
            fis.lba1 = (uint8_t)((lba48 >> 8) & 0xFF);
            fis.lba2 = (uint8_t)((lba48 >> 16) & 0xFF);
            fis.lba3 = (uint8_t)((lba48 >> 24) & 0xFF);
            fis.lba4 = (uint8_t)((lba48 >> 32) & 0xFF);
            fis.lba5 = (uint8_t)((lba48 >> 40) & 0xFF);
            fis.countl = (uint8_t)(chunk & 0xFF);
            fis.counth = (uint8_t)((chunk >> 8) & 0xFF);
        } else {
            if (lba >= (1u << 28))
                return -1;                          /* LBA28 limit */
            fis.command = write ? ATA_CMD_WRITE_DMA : ATA_CMD_READ_DMA;
            fis.lba0 = (uint8_t)(lba & 0xFF);
            fis.lba1 = (uint8_t)((lba >> 8) & 0xFF);
            fis.lba2 = (uint8_t)((lba >> 16) & 0xFF);
            fis.device = 0x40 | (uint8_t)((lba >> 24) & 0x0F);
            fis.countl = (uint8_t)(chunk & 0xFF);   /* 0 == 256 sectors */
        }

        if (ahci_command(p, &fis, p->bounce_phys, chunk * AHCI_SECTOR_SIZE,
                         write, g_use_irq) != 0)
            return -1;

        if (!write)
            memcpy((uint8_t *)buf + done * AHCI_SECTOR_SIZE,
                   p->bounce_virt, chunk * AHCI_SECTOR_SIZE);

        done += chunk;
        lba  += chunk;
    }
    return 0;
}

int ahci_read_sectors(int port, uint32_t lba, uint8_t count, void *buffer)
{
    unsigned long flags;

    if (port < 0 || port >= g_port_count || !g_ports[port].present)
        return -1;
    if (count == 0)
        return -1;

    /* sleep_on() must run with interrupts disabled; irq_save/restore
     * also makes this safe if ever called with IF=1. */
    flags = irq_save();
    int ret = ahci_dma_rw(&g_ports[port], lba, count, buffer, 0);
    irq_restore(flags);
    return ret;
}

int ahci_write_sectors(int port, uint32_t lba, uint8_t count,
                       const void *buffer)
{
    unsigned long flags;

    if (port < 0 || port >= g_port_count || !g_ports[port].present)
        return -1;
    if (count == 0)
        return -1;

    flags = irq_save();
    int ret = ahci_dma_rw(&g_ports[port], lba, count, (void *)buffer, 1);
    irq_restore(flags);
    return ret;
}

/* =========================================================================
 * Block device callbacks
 * ========================================================================= */

static int ahci_block_read(int prim_id, int scnd_id, void *buf,
                           uint32_t offset, size_t count)
{
    int port;

    (void)scnd_id;
    port = prim_id - AHCI_BASE_PRIM;
    if (port < 0 || port >= g_port_count || !g_ports[port].present)
        return -1;
    if (count == 0 || count > AHCI_MAX_SECTORS)
        return -1;

    if (ahci_read_sectors(port, offset, (uint8_t)count, buf) != 0)
        return -1;
    return (int)(count * AHCI_SECTOR_SIZE);
}

static int ahci_block_write(int prim_id, int scnd_id, const void *buf,
                            uint32_t offset, size_t count)
{
    int port;

    (void)scnd_id;
    port = prim_id - AHCI_BASE_PRIM;
    if (port < 0 || port >= g_port_count || !g_ports[port].present)
        return -1;
    if (count == 0 || count > AHCI_MAX_SECTORS)
        return -1;

    if (ahci_write_sectors(port, offset, (uint8_t)count, buf) != 0)
        return -1;
    return (int)(count * AHCI_SECTOR_SIZE);
}

/* =========================================================================
 * PCI detection + controller probing
 * ========================================================================= */

static void ahci_pci_callback(pci_device_t *pci_dev)
{
    static block_ops_t ahci_block_ops = {
        .read  = ahci_block_read,
        .write = ahci_block_write,
        .ioctl = NULL,
        .mmap  = NULL,
    };
    static const char *names[] = { "sda", "sdb", "sdc", "sdd" };

    if (g_ahci_found)
        return;                         /* only the first controller */

    if (pci_dev->prog_if != 0x01) {
        printk("[AHCI] SATA controller %04x:%04x is not AHCI "
               "(prog_if=0x%02x), skipping\n",
               pci_dev->vendor_id, pci_dev->device_id, pci_dev->prog_if);
        return;
    }

    printk("[AHCI] Found SATA controller: %04x:%04x (prog_if=0x01)\n",
           pci_dev->vendor_id, pci_dev->device_id);
    g_ahci_found = 1;

    /* Enable memory space + bus mastering (needed for DMA). */
    pci_enable_device(pci_dev->bus, pci_dev->slot, pci_dev->func,
                      PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER);

    /* ABAR: normally BAR5; a 64-bit BAR spans BAR4:BAR5. */
    if ((pci_dev->bar[4] & 0x7) == 0x4) {
        g_hba.abar_phys = pci_dev->bar[4] & 0xFFFFFFF0;
    } else {
        g_hba.abar_phys = pci_dev->bar[5] & 0xFFFFFFF0;
    }
    g_hba.irq = pci_dev->interrupt_line;

    g_hba.regs = (volatile uint32_t *)ioremap(g_hba.abar_phys,
                                              AHCI_ABAR_MAP_SIZE);
    if (!g_hba.regs) {
        printk("[AHCI] Failed to map ABAR 0x%08x\n", g_hba.abar_phys);
        g_ahci_found = 0;
        return;
    }

    g_hba.cap     = hba_read(AHCI_HBA_CAP);
    g_hba.version = hba_read(AHCI_HBA_VS);
    printk("[AHCI] ABAR=0x%08x IRQ=%u CAP=0x%08x VS=0x%08x\n",
           g_hba.abar_phys, g_hba.irq, g_hba.cap, g_hba.version);

    /* HBA reset (time bounded). */
    hba_write(AHCI_HBA_GHC, hba_read(AHCI_HBA_GHC) | AHCI_GHC_HR);
    {
        uint64_t deadline = ahci_deadline(AHCI_TIMEOUT_RESET_MS);

        while ((hba_read(AHCI_HBA_GHC) & AHCI_GHC_HR) &&
               ahci_rdtsc() < deadline)
            ahci_mdelay(1);
        if (hba_read(AHCI_HBA_GHC) & AHCI_GHC_HR) {
            printk("[AHCI] HBA reset timed out\n");
            return;
        }
    }

    /* Enter AHCI mode if the controller requires it. */
    if (g_hba.cap & AHCI_CAP_SAM)
        hba_write(AHCI_HBA_GHC, hba_read(AHCI_HBA_GHC) | AHCI_GHC_AE);

    {
        uint32_t pi = hba_read(AHCI_HBA_PI);
        int np = (g_hba.cap & AHCI_CAP_NP_MASK) + 1;
        printk("[AHCI] Ports implemented = 0x%08x, NP = %d\n", pi, np);

        /* Probe every implemented port. */
        for (int i = 0; i < np && i < AHCI_MAX_PORTS; i++) {
            ahci_port_t *p;
            int oom = 0;

            if (!(pi & (1u << i)))
                continue;

            p = &g_ports[g_port_count];
            memset(p, 0, sizeof(*p));
            p->index = i;
            p->active_slot = -1;
            init_waitqueue_head(&p->wq);

            /* Allocate DMA structures (contiguous, direct-mapped). */
            p->clb_virt    = page_alloc(1024);
            p->fb_virt     = page_alloc(256);
            p->bounce_virt = page_alloc(AHCI_MAX_SECTORS * AHCI_SECTOR_SIZE);
            if (!p->clb_virt || !p->fb_virt || !p->bounce_virt) {
                printk("[AHCI] Port %d: out of memory\n", i);
                continue;
            }
            p->clb_phys    = VIRT_TO_PHYS(p->clb_virt);
            p->fb_phys     = VIRT_TO_PHYS(p->fb_virt);
            p->bounce_phys = VIRT_TO_PHYS(p->bounce_virt);

            for (int s = 0; s < AHCI_MAX_SLOTS; s++) {
                p->ct_virt[s] = page_alloc(sizeof(ahci_cmd_table_t));
                if (!p->ct_virt[s]) {
                    oom = 1;
                    break;
                }
                p->ct_phys[s] = VIRT_TO_PHYS(p->ct_virt[s]);
            }
            if (oom) {
                printk("[AHCI] Port %d: out of memory (command tables)\n", i);
                continue;
            }

            if (ahci_port_start(p) != 0) {
                printk("[AHCI] Port %d: no link (SSTS=0x%08x)\n",
                       i, port_read(p, AHCI_PORT_SSTS));
                continue;
            }

            if (port_read(p, AHCI_PORT_SIG) != AHCI_SIG_ATA) {
                printk("[AHCI] Port %d: not an ATA device (SIG=0x%08x)\n",
                       i, port_read(p, AHCI_PORT_SIG));
                continue;
            }

            if (ahci_identify(p) != 0) {
                printk("[AHCI] Port %d: IDENTIFY failed\n", i);
                continue;
            }

            p->present = true;
            g_port_count++;
        }
    }

    /* Enable interrupts on the controller (global + per port via PxIE at
     * command time).  Fall back to polling if the IRQ is unusable. */
    if (g_hba.irq < IRQ_COUNT &&
        g_hba.irq != 0 && g_hba.irq != 1 && g_hba.irq != 12) {
        irq_register(g_hba.irq, ahci_isr);
        hba_write(AHCI_HBA_GHC, hba_read(AHCI_HBA_GHC) | AHCI_GHC_IE);
        g_use_irq = 1;
    } else {
        printk("[AHCI] IRQ %u not usable, using polling\n", g_hba.irq);
        g_use_irq = 0;
    }

    /* Register disks as block devices + devfs nodes + MBR sources. */
    for (int i = 0; i < g_port_count && i < 4; i++) {
        ahci_port_t *p = &g_ports[i];
        uint32_t size_mb = p->sectors / 2048;

        printk("[AHCI] %s: %s, %u MB (%u sectors)\n",
               names[i], p->model, size_mb, p->sectors);

        if (register_block_device(AHCI_BASE_PRIM + i, &ahci_block_ops) == 0)
            printk("[AHCI] Registered %s as block device %d\n",
                   names[i], AHCI_BASE_PRIM + i);
        else
            printk("[AHCI] Failed to register %s\n", names[i]);

        devfs_register_device(names[i], DT_BLKDEV, AHCI_BASE_PRIM + i, 0);
        mbr_register_disk(names[i], AHCI_BASE_PRIM, i,
                          ahci_read_sectors, ahci_write_sectors);
    }
}

void ahci_print_disks(void)
{
    for (int i = 0; i < g_port_count; i++) {
        ahci_port_t *p = &g_ports[i];
        if (p->present)
            printk("[AHCI] Disk %d (port %d): %s, %u MB (%u sectors)\n",
                   i, p->index, p->model,
                   p->sectors / 2048, p->sectors);
    }
}

int ahci_disk_count(void)
{
    return g_port_count;
}

void ahci_init(void)
{
    printk("[AHCI] Initializing AHCI driver...\n");

    /* Calibrate the TSC clock against the PIT for boot-time timeouts. */
    ahci_calibrate_clock();

    pci_scan_devices(PCI_CLASS_STORAGE, PCI_SUBCLASS_SATA,
                     ahci_pci_callback);

    if (!g_ahci_found) {
        printk("[AHCI] No AHCI controller found\n");
        return;
    }

    printk("[AHCI] Found %d SATA disk(s)\n", g_port_count);
}

