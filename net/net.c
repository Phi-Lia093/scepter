/* ============================================================================
 * Scepter Kernel - Networking Core
 *
 * Interface registry, per-interface receive queues, a static packet pool
 * (safe for IRQ context), and the raw transmit path.
 * ========================================================================= */

#include "net/net.h"
#include "lib/string.h"
#include "lib/printk.h"
#include "arch/cpu.h"

/* ============================================================================
 * Interface registry (static, allocation-free)
 * ========================================================================= */

#define MAX_NET_IFACES 8

static net_iface_t *g_ifaces[MAX_NET_IFACES];
static int          g_iface_count = 0;

/* ============================================================================
 * Packet pool
 *
 * Fixed-size pool so net_rx() never allocates from IRQ context.
 * ========================================================================= */

#define NET_POOL_SIZE 48

static net_pkt_t  g_pool[NET_POOL_SIZE];
static net_pkt_t *g_pool_free = NULL;

static void pool_init(void)
{
    for (int i = 0; i < NET_POOL_SIZE; i++) {
        g_pool[i].next = g_pool_free;
        g_pool_free = &g_pool[i];
    }
}

static net_pkt_t *pool_get(void)
{
    net_pkt_t *p = g_pool_free;
    if (p)
        g_pool_free = p->next;
    return p;
}

static void pool_put(net_pkt_t *p)
{
    p->next = g_pool_free;
    g_pool_free = p;
}

/* ============================================================================
 * Interface registration / lookup
 * ========================================================================= */

int net_register_iface(net_iface_t *iface)
{
    if (!iface || g_iface_count >= MAX_NET_IFACES)
        return -1;

    if (net_find_iface(iface->name))
        return -1;   /* duplicate */

    init_waitqueue_head(&iface->rx_wq);
    iface->rx_head = iface->rx_tail = NULL;
    iface->rx_count = 0;
    g_ifaces[g_iface_count++] = iface;
    return 0;
}

net_iface_t *net_find_iface(const char *name)
{
    if (!name)
        return NULL;
    for (int i = 0; i < g_iface_count; i++) {
        if (strcmp(g_ifaces[i]->name, name) == 0)
            return g_ifaces[i];
    }
    return NULL;
}

net_iface_t *net_get_iface(int index)
{
    if (index < 0 || index >= g_iface_count)
        return NULL;
    return g_ifaces[index];
}

/* ============================================================================
 * Transmit
 * ========================================================================= */

int net_transmit(net_iface_t *iface, const uint8_t *data, uint32_t len)
{
    if (!iface || !iface->transmit || !data)
        return -1;
    if (!(iface->flags & NET_IF_UP))
        return -1;
    if (len == 0 || len > NET_MAX_FRAME)
        return -1;

    int ret = iface->transmit(iface, data, len);
    if (ret == 0) {
        iface->stats.tx_packets++;
        iface->stats.tx_bytes += len;
    } else {
        iface->stats.tx_dropped++;
    }
    return ret;
}

/* ============================================================================
 * Receive
 * ========================================================================= */

void net_rx(net_iface_t *iface, const uint8_t *data, uint32_t len)
{
    if (!iface || !data || len == 0 || len > NET_MAX_FRAME)
        return;

    iface->stats.rx_packets++;
    iface->stats.rx_bytes += len;

    /* Offer to the in-kernel protocol layer (ARP/ICMP echo). */
    if (net_proto_rx(iface, data, len))
        return;   /* consumed, nothing for userspace */

    /* Otherwise queue the frame for userspace. */
    net_pkt_t *pkt = pool_get();
    if (!pkt) {
        iface->stats.rx_dropped++;
        return;
    }

    pkt->iface = iface;
    pkt->len   = len;
    memcpy(pkt->data, data, len);
    pkt->next  = NULL;

    if (iface->rx_tail) {
        iface->rx_tail->next = pkt;
        iface->rx_tail = pkt;
    } else {
        iface->rx_head = iface->rx_tail = pkt;
    }
    iface->rx_count++;

    /* Wake any process blocked in net_recv() on this interface. */
    wake_up(&iface->rx_wq);
}

int net_poll(net_iface_t *iface)
{
    return iface && iface->rx_head ? 1 : 0;
}

int net_recv(net_iface_t *iface, uint8_t *buf, uint32_t buflen)
{
    if (!iface || !buf)
        return 0;

    net_pkt_t *pkt = iface->rx_head;
    if (!pkt)
        return 0;

    iface->rx_head = pkt->next;
    if (!iface->rx_head)
        iface->rx_tail = NULL;
    iface->rx_count--;

    uint32_t n = (pkt->len < buflen) ? pkt->len : buflen;
    memcpy(buf, pkt->data, n);
    pool_put(pkt);
    return (int)n;
}

void net_set_ip(net_iface_t *iface, const uint8_t ip[4],
                const uint8_t netmask[4], const uint8_t gw[4])
{
    if (!iface)
        return;
    if (ip)      memcpy(iface->ip, ip, 4);
    if (netmask) memcpy(iface->netmask, netmask, 4);
    if (gw)      memcpy(iface->gw, gw, 4);
}

/* ============================================================================
 * Initialization
 * ========================================================================= */

void net_init(void)
{
    printk("[NET] Initializing networking core...\n");
    pool_init();

    /* The loopback interface is registered by net/loopback.c. */
    extern void loopback_init(void);
    loopback_init();

    printk("[NET] %d interface(s) registered\n", g_iface_count);
}
