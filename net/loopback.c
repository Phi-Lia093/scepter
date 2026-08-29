/* ============================================================================
 * Scepter Kernel - Loopback Interface
 *
 * "lo" is always up and always first in the registry.  Transmitting on it
 * loops the frame straight back into net_rx() (like real loopback).
 * ========================================================================= */

#include "net/net.h"
#include "lib/string.h"

static net_iface_t g_lo;

static int lo_transmit(net_iface_t *iface, const uint8_t *data, uint32_t len)
{
    /* Loop it straight back through the receive path. */
    net_rx(iface, data, len);
    return 0;
}

void loopback_init(void)
{
    memset(&g_lo, 0, sizeof(g_lo));
    strcpy(g_lo.name, "lo");
    g_lo.type    = NET_IFACE_LOOPBACK;
    g_lo.flags   = NET_IF_UP | NET_IF_LOOPBACK;
    g_lo.mtu     = NET_MTU_LOOPBACK;
    g_lo.ip[0]   = 127; g_lo.ip[1] = 0; g_lo.ip[2] = 0; g_lo.ip[3] = 1;
    g_lo.netmask[0] = 255; g_lo.netmask[1] = 0; g_lo.netmask[2] = 0; g_lo.netmask[3] = 0;
    g_lo.transmit = lo_transmit;

    net_register_iface(&g_lo);
}
