#ifndef AHCI_H
#define AHCI_H

#include <stdint.h>
#include <stdbool.h>

/* =========================================================================
 * AHCI (Advanced Host Controller Interface) SATA Driver
 *
 * Drives SATA disks through a PCI AHCI controller using DMA and interrupts:
 *   - detection via PCI (class 0x01, subclass 0x06, prog_if 0x01)
 *   - ABAR (BAR5) mapped with ioremap
 *   - runtime I/O: one DMA command per port, completion signalled by IRQ
 *   - boot-time IDENTIFY: polled (interrupts are still globally disabled)
 *
 * Block device numbering:
 *   prim 8-11  = sda-sdd          (raw disks)
 *   prim 12-15 = sdaX-sddX        (partitions, handled by part_mbr.c)
 * ========================================================================= */

/* -------------------------------------------------------------------------
 * HBA (Host Bus Adapter) registers
 * ------------------------------------------------------------------------- */
#define AHCI_HBA_CAP        0x00
#define AHCI_HBA_GHC        0x04
#define AHCI_HBA_IS         0x08
#define AHCI_HBA_PI         0x0C
#define AHCI_HBA_VS         0x10
#define AHCI_HBA_CAP2       0x24
#define AHCI_HBA_BOHC       0x28

/* GHC bits */
#define AHCI_GHC_HR         (1 << 0)    /* HBA reset */
#define AHCI_GHC_IE         (1 << 1)    /* global interrupt enable */
#define AHCI_GHC_AE         (1 << 31)   /* AHCI enable */

/* CAP bits */
#define AHCI_CAP_NP_MASK    0x1F        /* number of ports (NP = count-1) */
#define AHCI_CAP_SAM        (1 << 27)   /* supports AHCI mode only */

/* Size of ABAR we map (0x100 + 32 ports * 0x80 = 0x1100, round up). */
#define AHCI_ABAR_MAP_SIZE  0x2000

/* -------------------------------------------------------------------------
 * Port registers (port i base = 0x100 + i * 0x80)
 * ------------------------------------------------------------------------- */
#define AHCI_PORT_BASE      0x100
#define AHCI_PORT_STRIDE    0x80

#define AHCI_PORT_CLB       0x00
#define AHCI_PORT_CLBU      0x04
#define AHCI_PORT_FB        0x08
#define AHCI_PORT_FBU       0x0C
#define AHCI_PORT_IS        0x10
#define AHCI_PORT_IE        0x14
#define AHCI_PORT_CMD       0x18
#define AHCI_PORT_TFD       0x20
#define AHCI_PORT_SIG       0x24
#define AHCI_PORT_SSTS      0x28
#define AHCI_PORT_SCTL      0x2C
#define AHCI_PORT_SERR      0x30
#define AHCI_PORT_SACT      0x34
#define AHCI_PORT_CI        0x38
#define AHCI_PORT_SNTF      0x3C

/* PxCMD bits */
#define AHCI_CMD_ST         (1 << 0)    /* start */
#define AHCI_CMD_SUD        (1 << 1)    /* spin-up device */
#define AHCI_CMD_POD        (1 << 2)    /* power-on device */
#define AHCI_CMD_FRE        (1 << 4)    /* FIS receive enable */
#define AHCI_CMD_FR         (1 << 14)   /* FIS receive running */
#define AHCI_CMD_CR         (1 << 15)   /* command list running */

/* PxIS / PxIE bits */
#define AHCI_IS_DHRS        (1 << 0)    /* D2H register FIS received */
#define AHCI_IS_PSS         (1 << 1)    /* PIO setup FIS */
#define AHCI_IS_SDBS        (1 << 3)    /* set device bits FIS */
#define AHCI_IS_UFS         (1 << 4)    /* unknown FIS */
#define AHCI_IS_DPS         (1 << 5)    /* DMA setup FIS */
#define AHCI_IS_PCS         (1 << 6)    /* port change status */
#define AHCI_IS_PRCS        (1 << 22)   /* PHY ready change */
#define AHCI_IS_TFES        (1 << 30)   /* task file error status */
#define AHCI_IS_CPDS        (1 << 31)   /* cold port detect */

/* Bits we enable to complete a normal DMA command. */
#define AHCI_IE_COMPLETION  (AHCI_IS_DHRS | AHCI_IS_DPS | AHCI_IS_TFES)

/* PxSSTS */
#define AHCI_SSTS_DET       0x0F
#define AHCI_SSTS_DET_NODEV  0x00    /* no device detected / PHY offline */
#define AHCI_SSTS_DET_PRESENT 0x01   /* device present, no PHY comm yet */
#define AHCI_SSTS_DET_ERR    0x04    /* PHY error / offline */
#define AHCI_SSTS_DET_ONLINE 0x03
#define AHCI_SSTS_IPM       0xF00
#define AHCI_SSTS_IPM_ACTIVE 0x100

/* Boot-time wait timeouts, in milliseconds.  The detect window is what
 * empty ports wait before being skipped, so keep it short. */
#define AHCI_TIMEOUT_RESET_MS   1000   /* HBA reset (GHC.HR) */
#define AHCI_TIMEOUT_STOP_MS    1000   /* port stop (FR/CR clear) */
#define AHCI_TIMEOUT_FRE_MS     100    /* FIS receive engine start */
#define AHCI_TIMEOUT_DETECT_MS  50     /* device detection window */
#define AHCI_TIMEOUT_LINK_MS    2000   /* PHY online after detection */
#define AHCI_TIMEOUT_CMD_MS     5000   /* polled command completion */

/* PxSIG signatures */
#define AHCI_SIG_ATA        0x00000101
#define AHCI_SIG_ATAPI      0xEB140101

/* -------------------------------------------------------------------------
 * FIS / command structures
 * ------------------------------------------------------------------------- */
#define AHCI_FIS_H2D        0x27        /* host-to-device register FIS */

/* ATA commands used by this driver */
#define ATA_CMD_READ_DMA        0xC8
#define ATA_CMD_WRITE_DMA       0xCA
#define ATA_CMD_READ_DMA_EXT    0x25
#define ATA_CMD_WRITE_DMA_EXT   0x35
#define ATA_CMD_IDENTIFY        0xEC

#define AHCI_MAX_PORTS      32
#define AHCI_MAX_SLOTS      32          /* command slots per port */
#define AHCI_MAX_SECTORS    256         /* sectors per DMA command */
#define AHCI_SECTOR_SIZE    512

/* First prim_id used by AHCI disks. */
#define AHCI_BASE_PRIM      8

/* 20-byte H2D register FIS (5 dwords -> cfl = 5). */
typedef struct {
    uint8_t  fis_type;      /* AHCI_FIS_H2D */
    uint8_t  pmport_c;      /* bit7 = C (command), bits3:0 = PM port */
    uint8_t  command;
    uint8_t  featuresl;
    uint8_t  lba0;
    uint8_t  lba1;
    uint8_t  lba2;
    uint8_t  device;        /* 0x40 = LBA, master */
    uint8_t  lba3;
    uint8_t  lba4;
    uint8_t  lba5;
    uint8_t  featuresh;
    uint8_t  countl;
    uint8_t  counth;
    uint8_t  icc;
    uint8_t  control;
    uint8_t  rsv[4];
} __attribute__((packed)) ahci_fis_h2d_t;

/* Physical Region Descriptor Table entry (16 bytes). */
typedef struct {
    uint32_t dba;           /* data base address (low) */
    uint32_t dbau;          /* data base address (high) */
    uint32_t rsv0;
    uint32_t dbc;           /* bits21:0 = byte count - 1, bit31 = I */
} __attribute__((packed)) ahci_prdt_entry_t;

/* 256-byte command table: CFIS(0x00) + ATAPI(0x40) + reserved(0x50) +
 * PRDT at 0x80 (Linux AHCI_CMD_TBL_HDR_SZ), leaving room for 8 PRDs. */
typedef struct {
    uint8_t  cfis[64];
    uint8_t  acmd[16];
    uint8_t  rsv[48];
    ahci_prdt_entry_t prdt[8];
} __attribute__((packed)) ahci_cmd_table_t;

/* 32-byte command header. */
typedef struct {
    uint16_t cfl:5;         /* command FIS length in dwords */
    uint16_t a:1;           /* ATAPI */
    uint16_t w:1;           /* write */
    uint16_t p:1;           /* prefetchable */
    uint16_t r:1;           /* reset */
    uint16_t b:1;           /* BIST */
    uint16_t c:1;           /* clear busy on R_OK */
    uint16_t rsv0:1;
    uint16_t pmp:4;         /* port multiplier */
    uint16_t prdtl:16;      /* PRDT length (entries) */
    uint32_t prdbc;         /* PRD byte count (written by HBA) */
    uint32_t ctba;          /* command table base address (phys, low) */
    uint32_t ctbau;         /* command table base address (high) */
    uint32_t rsv1[4];
} __attribute__((packed)) ahci_cmd_hdr_t;

/* =========================================================================
 * Public API
 * ========================================================================= */

/** Detect the first AHCI controller, probe ports and register disks. */
void ahci_init(void);

/** Number of SATA disks currently detected by the AHCI driver. */
int ahci_disk_count(void);

/**
 * Read sectors from an AHCI disk using DMA + interrupt completion.
 * @param port   Disk index (0-based, matches prim_id - AHCI_BASE_PRIM)
 * @param lba    Starting LBA
 * @param count  Number of sectors (1-256)
 * @param buffer Destination (count * 512 bytes)
 * @return 0 on success, -1 on error
 */
int ahci_read_sectors(int port, uint32_t lba, uint8_t count, void *buffer);

/**
 * Write sectors to an AHCI disk using DMA + interrupt completion.
 * @return 0 on success, -1 on error
 */
int ahci_write_sectors(int port, uint32_t lba, uint8_t count, const void *buffer);

/** Print information about detected AHCI disks. */
void ahci_print_disks(void);

#endif /* AHCI_H */
