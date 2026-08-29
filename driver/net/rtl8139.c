/* ============================================================================
 * Scepter Kernel - Realtek RTL8139 Ethernet Driver
 *
 * Probes the PCI bus for an RTL8139 (10EC:8139), brings it up with an 8K
 * DMA RX ring + 4 TX buffers, registers "eth0" with the networking core,
 * and services RX/TX interrupts.
 *
 * All register semantics below are verified against QEMU's rtl8139
 * emulation (hw/net/rtl8139.c), which is the development target.
 * ========================================================================= */

#include "driver/net/rtl8139.h"
#include "driver/pci/pci.h"
#include "net/net.h"
#include "driver/apic/interrupt.h"
#include "kernel/asm.h"
#include "mm/buddy.h"
#include "mm/mm.h"
#include "lib/string.h"
#include "lib/printk.h"

/* ============================================================================
 * Per-NIC state (single NIC supported)
 * ========================================================================= */

static uint16_t     g_io_base = 0;
static net_iface_t  g_iface;
static uint8_t     *g_rx_ring = NULL;      /* virtual (direct-mapped)  */
static uint32_t     g_rx_phys = 0;         /* physical (for the NIC)   */
static uint8_t     *g_tx_buf[RTL_TX_DESC_COUNT] = {0};
static uint32_t     g_tx_phys[RTL_TX_DESC_COUNT] = {0};
static uint32_t     g_rx_cur  = 0;         /* driver read position     */
static int          g_tx_next = 0;         /* rotating TX descriptor   */

/* ============================================================================
 * Register access (I/O ports)
 * ========================================================================= */

static inline uint8_t  rtl_inb(uint16_t reg)  { return inb(g_io_base + reg); }
static inline uint16_t rtl_inw(uint16_t reg)  { return inw(g_io_base + reg); }
static inline uint32_t rtl_inl(uint16_t reg)  { return inl(g_io_base + reg); }
static inline void     rtl_outb(uint16_t reg, uint8_t v)  { outb(g_io_base + reg, v); }
static inline void     rtl_outw(uint16_t reg, uint16_t v) { outw(g_io_base + reg, v); }
static inline void     rtl_outl(uint16_t reg, uint32_t v) { outl(g_io_base + reg, v); }


/* ============================================================================
 * Transmit
 * ========================================================================= */

static int rtl8139_transmit_frame(net_iface_t *iface,
                                  const uint8_t *data, uint32_t len)
{
    (void)iface;

    if (len == 0 || len > RTL_TX_BUF_SIZE)
        return -1;

    /* Try each descriptor (rotating) until one is owned by the NIC again.
     * A descriptor is usable when its OWN bit (0x2000) is set; writing the
     * length clears it, which triggers the DMA transfer. */
    for (int spin = 0; spin < RTL_TX_DESC_COUNT * 8; spin++) {
        int desc = g_tx_next;
        if (rtl_inl(RTL_REG_TxStatus0 + desc * 4) & RTL_TX_HOST_OWNS) {
            memcpy(g_tx_buf[desc], data, len);
            rtl_outl(RTL_REG_TxAddr0 + desc * 4, g_tx_phys[desc]);
            rtl_outl(RTL_REG_TxStatus0 + desc * 4, len);   /* start TX */

            /* Wait for completion (QEMU completes synchronously; the OWN
             * bit is re-set by the NIC when it is done). */
            for (int wait = 0; wait < 100000; wait++) {
                if (rtl_inl(RTL_REG_TxStatus0 + desc * 4) & RTL_TX_HOST_OWNS)
                    break;
            }

            g_tx_next = (g_tx_next + 1) % RTL_TX_DESC_COUNT;
            return 0;
        }
        g_tx_next = (g_tx_next + 1) % RTL_TX_DESC_COUNT;
    }
    return -1;   /* all descriptors busy */
}

/* ============================================================================
 * Receive
 *
 * Packets live in the 8K ring: [2-byte status][2-byte length][data][CRC].
 * The length word includes the 4-byte CRC.  QEMU may split a packet across
 * the ring end, so the data is reassembled into a stack buffer first.
 * ========================================================================= */

static void rtl8139_receive(void)
{
    while (1) {
        uint16_t cba = rtl_inw(RTL_REG_CBA);   /* NIC write pointer */
        if (g_rx_cur == cba)
            break;   /* caught up */

        /* A packet is pending at the current read position. */
        uint8_t *pkt = g_rx_ring + g_rx_cur;
        uint16_t status = pkt[0] | (pkt[1] << 8);
        uint16_t rx_len = pkt[2] | (pkt[3] << 8);   /* includes CRC */

        if (rx_len < 4 || rx_len > RTL_RX_BUF_SIZE)
            break;   /* bogus length - stop to avoid looping */

        uint32_t data_len = rx_len - 4;   /* strip CRC */
        uint32_t data_off = g_rx_cur + 4;

        if (status & RTL_RX_STATUS_OK) {
            /* Reassemble (packet may wrap around the ring end). */
            uint8_t tmp[NET_MAX_FRAME];
            if (data_off + data_len <= RTL_RX_BUF_SIZE) {
                memcpy(tmp, g_rx_ring + data_off, data_len);
            } else {
                uint32_t part1 = RTL_RX_BUF_SIZE - data_off;
                memcpy(tmp, g_rx_ring + data_off, part1);
                memcpy(tmp + part1, g_rx_ring, data_len - part1);
            }
            net_rx(&g_iface, tmp, data_len);
        } else {
            g_iface.stats.rx_dropped++;
        }

        /* Advance read position to the next packet (4-byte aligned). */
        g_rx_cur = (g_rx_cur + 4 + rx_len + 3) & ~3;
        if (g_rx_cur >= RTL_RX_BUF_SIZE)
            g_rx_cur -= RTL_RX_BUF_SIZE;

        /* Tell the NIC how much we consumed.  CAPR is written "off by 16"
         * so the hardware can tell empty from full. */
        rtl_outw(RTL_REG_CAPR, (uint16_t)(g_rx_cur - 16));
    }
}

/* ============================================================================
 * IRQ handler
 * ========================================================================= */

static void rtl8139_irq(uint32_t cs)
{
    (void)cs;

    uint16_t status = rtl_inw(RTL_REG_IntrStatus);
    if (!status)
        return;

    /* Acknowledge BEFORE processing receive buffers (QEMU behaviour). */
    rtl_outw(RTL_REG_IntrStatus, status);

    if (status & RTL_ISR_RXOK)
        rtl8139_receive();

    if (status & RTL_ISR_TXOK) {
        /* Transmit completion is polled in transmit_frame(); nothing else
         * to do here. */
    }

    if (status & RTL_ISR_RX_OVERFLOW) {
        /* Ring overran: reset the read pointer and let the NIC continue. */
        g_iface.stats.rx_dropped++;
        rtl_outw(RTL_REG_CAPR, (uint16_t)(g_rx_cur - 16));
    }

    if (status & (RTL_ISR_RXERR | RTL_ISR_TXERR))
        g_iface.stats.rx_dropped++;
}

/* ============================================================================
 * Device bring-up
 * ========================================================================= */

static void rtl8139_reset_chip(void)
{
    rtl_outb(RTL_REG_ChipCmd, RTL_CMD_RESET);
    for (int i = 0; i < 100; i++) {
        if (!(rtl_inb(RTL_REG_ChipCmd) & RTL_CMD_RESET))
            break;
    }
}

static void rtl8139_init_device(pci_device_t *dev)
{
    g_io_base = dev->bar[0] & 0xFFFC;

    if (dev->interrupt_line >= 16) {
        printk("[RTL8139] IRQ %d unsupported (need 0-15)\n",
               dev->interrupt_line);
        return;
    }

    /* Interface state lives in a static struct; wipe it first. */
    memset(&g_iface, 0, sizeof(g_iface));

    /* Allocate DMA structures (page-aligned, contiguous, direct-mapped). */
    g_rx_ring = (uint8_t *)page_alloc(RTL_RX_BUF_TOTAL);
    for (int i = 0; i < RTL_TX_DESC_COUNT; i++)
        g_tx_buf[i] = (uint8_t *)page_alloc(RTL_TX_BUF_SIZE);
    if (!g_rx_ring || !g_tx_buf[0] || !g_tx_buf[1] ||
        !g_tx_buf[2] || !g_tx_buf[3]) {
        printk("[RTL8139] Out of memory for DMA buffers\n");
        return;
    }
    g_rx_phys = VIRT_TO_PHYS(g_rx_ring);
    for (int i = 0; i < RTL_TX_DESC_COUNT; i++)
        g_tx_phys[i] = VIRT_TO_PHYS(g_tx_buf[i]);

    /* Software reset. */
    rtl8139_reset_chip();

    /* Unlock config registers for the setup writes. */
    rtl_outb(RTL_REG_Cfg9346, RTL_CFG9346_UNLOCK);

    /* Read the MAC address. */
    for (int i = 0; i < 6; i++)
        g_iface.mac[i] = rtl_inb(RTL_REG_IDR0 + i);

    /* Zero the multicast filter. */
    for (int i = 0; i < 8; i++)
        rtl_outb(RTL_REG_MAR0 + i, 0);

    /* Tx/Rx configuration. */
    rtl_outl(RTL_REG_TxConfig, RTL_TX_CONFIG);
    rtl_outl(RTL_REG_RxConfig, RTL_RX_CONFIG);   /* also resets the ring */

    /* Lock config registers again. */
    rtl_outb(RTL_REG_Cfg9346, RTL_CFG9346_LOCK);

    /* Program the RX ring base and reset our read pointer. */
    rtl_outl(RTL_REG_RxBuf, g_rx_phys);
    g_rx_cur = 0;

    /* Enable receiver + transmitter. */
    rtl_outb(RTL_REG_ChipCmd, RTL_CMD_RX_ENB | RTL_CMD_TX_ENB);

    /* Register the interface with the networking core. */
    strcpy(g_iface.name, "eth0");
    g_iface.type  = NET_IFACE_ETHERNET;
    g_iface.flags = NET_IF_UP;
    g_iface.mtu   = NET_MTU_ETHERNET;
    g_iface.transmit = rtl8139_transmit_frame;
    /* Static SLIRP configuration (QEMU user networking defaults). */
    g_iface.ip[0] = 10; g_iface.ip[1] = 0; g_iface.ip[2] = 2; g_iface.ip[3] = 15;
    g_iface.netmask[0] = 255; g_iface.netmask[1] = 255;
    g_iface.netmask[2] = 255; g_iface.netmask[3] = 0;
    g_iface.gw[0] = 10; g_iface.gw[1] = 0; g_iface.gw[2] = 2; g_iface.gw[3] = 2;

    if (net_register_iface(&g_iface) != 0) {
        printk("[RTL8139] Failed to register eth0\n");
        return;
    }

    /* Clear any pending interrupt, then enable and register the IRQ. */
    rtl_outw(RTL_REG_IntrStatus, 0xFFFF);
    rtl_outw(RTL_REG_IntrMask, RTL_IMR_DEFAULT);
    irq_register(dev->interrupt_line, rtl8139_irq);

    uint16_t bmsr = rtl_inw(RTL_REG_BMSR);
    printk("[RTL8139] %02x:%02x.%x eth0 MAC %02x:%02x:%02x:%02x:%02x:%02x"
           " IO 0x%04x IRQ %d (link %s)\n",
           dev->bus, dev->slot, dev->func,
           g_iface.mac[0], g_iface.mac[1], g_iface.mac[2],
           g_iface.mac[3], g_iface.mac[4], g_iface.mac[5],
           g_io_base, dev->interrupt_line,
           (bmsr & 0x04) ? "up" : "down");
}


/* ============================================================================
 * PCI probe + entry point
 * ========================================================================= */

static void rtl8139_pci_callback(pci_device_t *dev)
{
    /* The NIC needs I/O space + bus mastering for DMA. */
    pci_enable_device(dev->bus, dev->slot, dev->func,
                      PCI_COMMAND_IO | PCI_COMMAND_MASTER);
    rtl8139_init_device(dev);
}

void rtl8139_init(void)
{
    printk("[RTL8139] Probing PCI for Realtek RTL8139...\n");
    pci_scan_devices(PCI_CLASS_NETWORK, PCI_SUBCLASS_ETHERNET,
                     rtl8139_pci_callback);
}

