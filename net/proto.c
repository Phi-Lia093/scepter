/* ============================================================================
 * Scepter Kernel - Minimal Protocol Layer
 *
 * Handles enough of ARP and IPv4/ICMP that the kernel can answer ping and
 * ARP requests itself (in IRQ context, no allocation).  Everything else is
 * passed through to the userspace receive queue.
 *
 *   - ARP:      reply to requests for our IP on ethernet interfaces
 *   - IPv4/ICMP: reply to echo requests addressed to our IP (eth + loopback)
 *   - all other frames: forwarded unchanged to userspace
 * ========================================================================= */

#include "net/net.h"
#include "lib/string.h"

/* ---- Ethernet / ARP / IP constants ---- */
#define ETH_HDR_LEN     14
#define ETH_TYPE_IPV4   0x0800
#define ETH_TYPE_ARP    0x0806
#define ARP_HRD_ETH     1
#define ARP_OP_REQUEST  1
#define ARP_OP_REPLY    2
#define ARP_HLEN        28
#define IP_PROTO_ICMP   1
#define ICMP_ECHO_REPLY   0
#define ICMP_ECHO_REQUEST 8

/* ---- Big-endian helpers ---- */
static uint16_t be16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
static void put_be16(uint8_t *p, uint16_t v) { p[0] = v >> 8; p[1] = v & 0xFF; }

/* ---- Ones-complement checksum (RFC 1071 style) ---- */
static uint16_t net_checksum(const uint8_t *data, uint32_t len)
{
    uint32_t sum = 0;
    while (len > 1) {
        sum += (uint16_t)((data[0] << 8) | data[1]);
        data += 2;
        len  -= 2;
    }
    if (len)
        sum += (uint16_t)(data[0] << 8);
    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)~sum;
}

/* ============================================================================
 * ARP handling (ethernet interfaces only)
 * ========================================================================= */

static int arp_rx(net_iface_t *iface, const uint8_t *frame, uint32_t len)
{
    if (len < ETH_HDR_LEN + ARP_HLEN)
        return 0;
    if (be16(frame + 12) != ETH_TYPE_ARP)
        return 0;

    const uint8_t *a = frame + ETH_HDR_LEN;   /* ARP payload */

    if (be16(a + 0) != ARP_HRD_ETH)      return 0;
    if (be16(a + 2) != ETH_TYPE_IPV4)    return 0;
    if (a[4] != NET_MAC_LEN || a[5] != 4) return 0;

    uint16_t op = be16(a + 6);

    if (op == ARP_OP_REQUEST) {
        /* Request for OUR IP: reply. Layout: sha[6] spa[4] tha[6] tpa[4] */
        if (memcmp(a + 30, iface->ip, 4) != 0)
            return 0;   /* not for us - pass through */

        uint8_t reply[ETH_HDR_LEN + ARP_HLEN];
        memcpy(reply + 0, a + 14, 6);           /* dst = requester MAC */
        memcpy(reply + 6, iface->mac, 6);       /* src = our MAC       */
        put_be16(reply + 12, ETH_TYPE_ARP);

        uint8_t *r = reply + ETH_HDR_LEN;
        put_be16(r + 0, ARP_HRD_ETH);
        put_be16(r + 2, ETH_TYPE_IPV4);
        r[4] = NET_MAC_LEN; r[5] = 4;
        put_be16(r + 6, ARP_OP_REPLY);
        memcpy(r + 8,  iface->mac, 6);          /* sha = our MAC       */
        memcpy(r + 14, iface->ip,  4);          /* spa = our IP        */
        memcpy(r + 18, a + 14, 6);              /* tha = requester MAC */
        memcpy(r + 24, a + 20, 4);              /* tpa = requester IP  */

        net_transmit(iface, reply, sizeof(reply));
        return 1;   /* consumed */
    }

    /* ARP replies are forwarded to userspace (ping needs them). */
    return 0;
}

/* ============================================================================
 * IPv4 / ICMP handling
 *
 * icmp_rx_ip handles a raw IPv4 packet (loopback, or payload of an
 * ethernet frame).  src_mac is the sender's MAC when the packet came over
 * ethernet (used for the reply frame); NULL for loopback.
 * ========================================================================= */

static int icmp_rx_ip(net_iface_t *iface, const uint8_t *ip, uint32_t iplen,
                      const uint8_t *src_mac)
{
    if (iplen < 20)
        return 0;
    if ((ip[0] >> 4) != 4)              /* IPv4 */
        return 0;
    uint32_t ihlen = (ip[0] & 0x0F) * 4;
    if (ihlen < 20 || iplen < ihlen)
        return 0;
    if (ip[9] != IP_PROTO_ICMP)
        return 0;
    if (memcmp(ip + 16, iface->ip, 4) != 0)   /* dst IP must be ours */
        return 0;

    const uint8_t *icmp = ip + ihlen;
    uint32_t icmplen = iplen - ihlen;
    if (icmplen < 8)
        return 0;
    if (icmp[0] != ICMP_ECHO_REQUEST)
        return 0;   /* not an echo request - forward to userspace */

    /* Build the echo reply.  Ethernet header (if applicable) + IP header
     * with src/dst swapped + ICMP message with type changed to REPLY. */
    uint8_t buf[NET_MAX_FRAME];
    uint32_t off = 0;

    if (src_mac) {
        memcpy(buf + 0, src_mac, 6);        /* dst = requester MAC  */
        memcpy(buf + 6, iface->mac, 6);     /* src = our MAC        */
        put_be16(buf + 12, ETH_TYPE_IPV4);
        off = ETH_HDR_LEN;
    }

    uint8_t *r = buf + off;

    /* IP header */
    memcpy(r, ip, ihlen);
    r[0] = 0x45;                            /* IPv4, 20-byte header */
    put_be16(r + 2, (uint16_t)iplen);       /* total length         */
    put_be16(r + 4, 0);                     /* id                   */
    put_be16(r + 6, 0);                     /* flags/frag           */
    r[8] = 64;                              /* TTL                  */
    r[9] = IP_PROTO_ICMP;
    memcpy(r + 12, iface->ip, 4);           /* src = our IP         */
    memcpy(r + 16, ip + 12, 4);             /* dst = requester IP   */
    put_be16(r + 10, 0);                    /* checksum zero first  */
    put_be16(r + 10, net_checksum(r, ihlen));

    /* ICMP message */
    memcpy(r + ihlen, icmp, icmplen);
    r[ihlen] = ICMP_ECHO_REPLY;
    uint16_t cs = net_checksum(r + ihlen, icmplen);
    put_be16(r + ihlen + 2, cs);

    net_transmit(iface, buf, off + iplen);
    return 1;   /* consumed */
}

static int icmp_rx_eth(net_iface_t *iface, const uint8_t *frame, uint32_t len)
{
    if (len < ETH_HDR_LEN + 20)
        return 0;
    if (be16(frame + 12) != ETH_TYPE_IPV4)
        return 0;
    return icmp_rx_ip(iface, frame + ETH_HDR_LEN, len - ETH_HDR_LEN,
                      frame + 6);   /* source MAC */
}

/* ============================================================================
 * Entry point (called from net_rx)
 * ========================================================================= */

int net_proto_rx(net_iface_t *iface, const uint8_t *data, uint32_t len)
{
    if (!iface || !data)
        return 0;

    if (iface->type == NET_IFACE_LOOPBACK) {
        /* Loopback frames are raw IPv4 packets. */
        if (icmp_rx_ip(iface, data, len, NULL))
            return 1;
        return 0;
    }

    /* Ethernet interfaces: ARP then IPv4/ICMP. */
    if (arp_rx(iface, data, len))
        return 1;
    if (icmp_rx_eth(iface, data, len))
        return 1;
    return 0;
}

