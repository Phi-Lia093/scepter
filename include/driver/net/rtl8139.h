#ifndef RTL8139_H
#define RTL8139_H

#include <stdint.h>

/* =========================================================================
 * Realtek RTL8139 Fast Ethernet Controller driver.
 *
 * Register offsets follow the QEMU emulation map (also the OSDev/8139too
 * convention), which is what the kernel is developed and tested against:
 *
 *   0x00 IDR0-5    MAC address
 *   0x08 MAR0-7    multicast filter
 *   0x10 TxStatus0-3   transmit status / length (4 x 32-bit)
 *   0x20 TxAddr0-3     transmit buffer physical address (4 x 32-bit)
 *   0x30 RxBuf         RX ring physical base address
 *   0x37 ChipCmd       command: 0x10 reset, 0x08 RX enable, 0x04 TX enable
 *   0x38 RxBufPtr      CAPR - driver read pointer (off by 16)
 *   0x3A RxBufAddr     CBA  - NIC write pointer (read-only)
 *   0x3C IntrMask      interrupt mask
 *   0x3E IntrStatus    interrupt status (write 1 to clear)
 *   0x40 TxConfig
 *   0x44 RxConfig      ring size = bits[12:11] (00 = 8K)
 *   0x50 Cfg9346       config write enable (0xC0 unlock / 0x00 lock)
 *   0x52 Config1
 *   0x62 BasicModeCtrl (BMCR)
 *   0x64 BasicModeStatus (BMSR) - bit 2 = link status
 * ========================================================================= */

/* PCI identity */
#define RTL8139_VENDOR_ID  0x10EC
#define RTL8139_DEVICE_ID  0x8139

/* Register offsets */
#define RTL_REG_IDR0       0x00
#define RTL_REG_MAR0       0x08
#define RTL_REG_TxStatus0  0x10
#define RTL_REG_TxAddr0    0x20
#define RTL_REG_RxBuf      0x30
#define RTL_REG_ChipCmd    0x37
#define RTL_REG_CAPR       0x38   /* driver read pointer (off by 16) */
#define RTL_REG_CBA        0x3A   /* NIC write pointer (read-only)   */
#define RTL_REG_IntrMask   0x3C
#define RTL_REG_IntrStatus 0x3E
#define RTL_REG_TxConfig   0x40
#define RTL_REG_RxConfig   0x44
#define RTL_REG_Cfg9346    0x50
#define RTL_REG_Config1    0x52
#define RTL_REG_BMCR       0x62
#define RTL_REG_BMSR       0x64

/* ChipCmd bits */
#define RTL_CMD_RESET      0x10
#define RTL_CMD_RX_ENB     0x08
#define RTL_CMD_TX_ENB     0x04
#define RTL_CMD_RX_BUF_EMPTY 0x01

/* Cfg9346 bits */
#define RTL_CFG9346_UNLOCK 0xC0
#define RTL_CFG9346_LOCK   0x00

/* Interrupt status/mask bits (QEMU naming) */
#define RTL_ISR_RXOK       0x0001
#define RTL_ISR_RXERR      0x0002
#define RTL_ISR_TXOK       0x0004
#define RTL_ISR_TXERR      0x0008
#define RTL_ISR_RX_OVERFLOW 0x0010
#define RTL_ISR_RX_UNDERRUN 0x0020
#define RTL_ISR_RX_FIFO_OVER 0x0040

/* Default interrupt mask: everything we handle */
#define RTL_IMR_DEFAULT    (RTL_ISR_RXOK | RTL_ISR_RXERR | RTL_ISR_TXOK | \
                            RTL_ISR_TXERR | RTL_ISR_RX_OVERFLOW)

/* TxStatus bits */
#define RTL_TX_HOST_OWNS   0x2000   /* set = NIC owns descriptor */
#define RTL_TX_STAT_OK     0x8000   /* set = transmission completed */
#define RTL_TX_LEN_MASK    0x1FFF   /* low 13 bits = buffer length */

/* RxStatus bits (packet header, first word) */
#define RTL_RX_STATUS_OK   0x0001

/* Ring geometry: 8K ring + 16-byte slack (hardware requires it). */
#define RTL_RX_BUF_SIZE    8192
#define RTL_RX_BUF_TOTAL   (RTL_RX_BUF_SIZE + 16)
#define RTL_TX_BUF_SIZE    2048
#define RTL_TX_DESC_COUNT  4

/* RxConfig: accept our own physical address (0x02), multicast (0x04) and
 * broadcast (0x08); ring size bits [12:11] = 00 (8K).  Bit values follow
 * QEMU / the RTL8139 datasheet (AcceptMyPhys is bit 1, NOT bit 5). */
#define RTL_RX_CONFIG      0x0000000E
/* TxConfig: standard init value used by 8139too/OSDev. */
#define RTL_TX_CONFIG      0x03000660

/**
 * Scan the PCI bus for an RTL8139, initialize it and register the "eth0"
 * interface with the networking core.  Safe to call if no NIC is present.
 */
void rtl8139_init(void);

#endif /* RTL8139_H */
