/* ping - send ICMP echo requests and report replies
 *
 * Uses the raw-frame net syscalls:
 *   - loopback targets (127.0.0.1) are answered by the kernel protocol
 *     layer, which consumes the echo request and queues the echo reply;
 *   - ethernet targets go through ARP resolution (the kernel forwards ARP
 *     replies to userspace) and the ICMP reply is read back from eth0.
 */
#include "stdio.h"
#include "string.h"
#include "stdlib.h"
#include "errno.h"
#include "unistd.h"
#include "sys/net.h"

#define ETH_HDR_LEN    14
#define ETH_TYPE_IPV4  0x0800
#define ETH_TYPE_ARP   0x0806
#define ARP_OP_REQUEST 1
#define ARP_OP_REPLY   2
#define ARP_HLEN       28
#define IP_PROTO_ICMP  1
#define ICMP_ECHO_REPLY    0
#define ICMP_ECHO_REQUEST  8
#define ICMP_PAYLOAD   32

/* ---- match state for wait_match() ---- */
static uint8_t  g_match_ip[4];
static uint16_t g_match_id;

/* ---- helpers ---- */

static uint16_t chksum(const uint8_t *data, uint32_t len)
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

static void put16(uint8_t *p, uint16_t v) { p[0] = v >> 8; p[1] = v & 0xFF; }
static uint16_t get16(const uint8_t *p)   { return (uint16_t)((p[0] << 8) | p[1]); }

static int parse_ip(const char *s, uint8_t out[4])
{
    int part = 0, val = 0, digits = 0;
    const char *p = s;
    for (;; p++) {
        if (*p >= '0' && *p <= '9') {
            val = val * 10 + (*p - '0');
            digits++;
            if (val > 255 || digits > 3)
                return -1;
        } else if (*p == '.' || *p == '\0') {
            if (digits == 0 || part > 3)
                return -1;
            out[part++] = (uint8_t)val;
            val = 0;
            digits = 0;
            if (*p == '\0')
                break;
        } else {
            return -1;
        }
    }
    return (part == 4) ? 0 : -1;
}

static int ip_eq(const uint8_t a[4], const uint8_t b[4])
{
    return a[0] == b[0] && a[1] == b[1] && a[2] == b[2] && a[3] == b[3];
}

static int ip_is_zero(const uint8_t ip[4])
{
    return ip[0] == 0 && ip[1] == 0 && ip[2] == 0 && ip[3] == 0;
}

/* 1 if target is on the same subnet as ip (masked by netmask). */
static int ip_on_subnet(const uint8_t ip[4], const uint8_t netmask[4],
                        const uint8_t target[4])
{
    return (ip[0] & netmask[0]) == (target[0] & netmask[0]) &&
           (ip[1] & netmask[1]) == (target[1] & netmask[1]) &&
           (ip[2] & netmask[2]) == (target[2] & netmask[2]) &&
           (ip[3] & netmask[3]) == (target[3] & netmask[3]);
}

static void print_ip(const uint8_t ip[4])
{
    printf("%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
}

/* ---- packet builders ---- */

/* Ethernet + IPv4 + ICMP echo request. */
static int build_eth_icmp(uint8_t *out, const uint8_t src_mac[6],
                          const uint8_t dst_mac[6], const uint8_t src_ip[4],
                          const uint8_t dst_ip[4], uint16_t id, uint16_t seq)
{
    int iplen = 20 + 8 + ICMP_PAYLOAD;
    memset(out, 0, ETH_HDR_LEN + iplen);
    memcpy(out + 0, dst_mac, 6);
    memcpy(out + 6, src_mac, 6);
    put16(out + 12, ETH_TYPE_IPV4);

    uint8_t *ip = out + ETH_HDR_LEN;
    ip[0] = 0x45;
    put16(ip + 2, (uint16_t)iplen);
    put16(ip + 4, id);
    ip[8] = 64;
    ip[9] = IP_PROTO_ICMP;
    memcpy(ip + 12, src_ip, 4);
    memcpy(ip + 16, dst_ip, 4);
    put16(ip + 10, 0);
    put16(ip + 10, chksum(ip, 20));

    uint8_t *icmp = ip + 20;
    icmp[0] = ICMP_ECHO_REQUEST;
    put16(icmp + 2, 0);
    put16(icmp + 4, id);
    put16(icmp + 6, seq);
    put16(icmp + 2, chksum(icmp, 8 + ICMP_PAYLOAD));
    return ETH_HDR_LEN + iplen;
}

/* Raw IPv4 + ICMP echo request (loopback). */
static int build_ip_icmp(uint8_t *out, const uint8_t src_ip[4],
                         const uint8_t dst_ip[4], uint16_t id, uint16_t seq)
{
    int iplen = 20 + 8 + ICMP_PAYLOAD;
    memset(out, 0, iplen);

    uint8_t *ip = out;
    ip[0] = 0x45;
    put16(ip + 2, (uint16_t)iplen);
    put16(ip + 4, id);
    ip[8] = 64;
    ip[9] = IP_PROTO_ICMP;
    memcpy(ip + 12, src_ip, 4);
    memcpy(ip + 16, dst_ip, 4);
    put16(ip + 10, 0);
    put16(ip + 10, chksum(ip, 20));

    uint8_t *icmp = ip + 20;
    icmp[0] = ICMP_ECHO_REQUEST;
    put16(icmp + 2, 0);
    put16(icmp + 4, id);
    put16(icmp + 6, seq);
    put16(icmp + 2, chksum(icmp, 8 + ICMP_PAYLOAD));
    return iplen;
}

/* Ethernet + ARP request "who has dst_ip". */
static int build_arp_request(uint8_t *out, const uint8_t mac[6],
                             const uint8_t src_ip[4], const uint8_t dst_ip[4])
{
    memset(out, 0, ETH_HDR_LEN + ARP_HLEN);
    memset(out + 0, 0xFF, 6);            /* broadcast */
    memcpy(out + 6, mac, 6);
    put16(out + 12, ETH_TYPE_ARP);

    uint8_t *a = out + ETH_HDR_LEN;
    put16(a + 0, 1);                     /* htype: ethernet */
    put16(a + 2, ETH_TYPE_IPV4);
    a[4] = 6;
    a[5] = 4;
    put16(a + 6, ARP_OP_REQUEST);
    memcpy(a + 8,  mac, 6);              /* sha */
    memcpy(a + 14, src_ip, 4);           /* spa */
    memset(a + 18, 0, 6);                /* tha */
    memcpy(a + 24, dst_ip, 4);           /* tpa */
    return ETH_HDR_LEN + ARP_HLEN;
}

/* ---- receive helpers ---- */

/* Poll net_recv() non-blocking until match(buf,len) returns 1 (~3s timeout).
 * Returns 1 with the frame in out, or 0 on timeout. */
static int wait_match(const char *ifname,
                      int (*match)(const uint8_t *buf, int len),
                      uint8_t *out, int *out_len)
{
    for (int i = 0; i < 40; i++) {
        uint8_t buf[NET_MAX_FRAME];
        int n = net_recv(ifname, buf, sizeof(buf), 0);
        if (n > 0 && match(buf, n)) {
            memcpy(out, buf, n);
            *out_len = n;
            return 1;
        }
        usleep(75000);
    }
    return 0;
}

/* ICMP echo reply over ethernet (frame[12..13] = 0x0800). */
static int match_eth_reply(const uint8_t *buf, int len)
{
    if (len < ETH_HDR_LEN + 20)
        return 0;
    if (get16(buf + 12) != ETH_TYPE_IPV4)
        return 0;
    const uint8_t *ip = buf + ETH_HDR_LEN;
    if ((ip[0] >> 4) != 4 || ip[9] != IP_PROTO_ICMP)
        return 0;
    if (!ip_eq(ip + 12, g_match_ip))
        return 0;
    const uint8_t *icmp = ip + 20;
    if (icmp[0] != ICMP_ECHO_REPLY)
        return 0;
    return get16(icmp + 4) == g_match_id;
}

/* ICMP echo reply as a raw IPv4 packet (loopback). */
static int match_lo_reply(const uint8_t *buf, int len)
{
    if (len < 20)
        return 0;
    const uint8_t *ip = buf;
    if ((ip[0] >> 4) != 4 || ip[9] != IP_PROTO_ICMP)
        return 0;
    if (!ip_eq(ip + 12, g_match_ip))
        return 0;
    const uint8_t *icmp = ip + 20;
    if (icmp[0] != ICMP_ECHO_REPLY)
        return 0;
    return get16(icmp + 4) == g_match_id;
}

/* ARP reply for our target. */
static int match_arp_reply(const uint8_t *buf, int len)
{
    if (len < ETH_HDR_LEN + ARP_HLEN)
        return 0;
    if (get16(buf + 12) != ETH_TYPE_ARP)
        return 0;
    const uint8_t *a = buf + ETH_HDR_LEN;
    if (get16(a + 6) != ARP_OP_REPLY)
        return 0;
    return ip_eq(a + 14, g_match_ip);   /* spa == target */
}

/* Resolve next_hop_ip's MAC via ARP. Returns 1 and fills next_hop_mac. */
static int arp_resolve(const net_ifconfig_t *eth, const uint8_t next_hop_ip[4],
                       uint8_t next_hop_mac[6])
{
    uint8_t req[ETH_HDR_LEN + ARP_HLEN];
    int rlen = build_arp_request(req, eth->mac, eth->ip, next_hop_ip);
    if (net_send("eth0", req, rlen) < 0)
        return 0;

    memcpy(g_match_ip, next_hop_ip, 4);
    uint8_t buf[NET_MAX_FRAME];
    int blen = 0;
    if (!wait_match("eth0", match_arp_reply, buf, &blen))
        return 0;
    memcpy(next_hop_mac, buf + ETH_HDR_LEN + 8, 6);   /* sha from reply */
    return 1;
}


static int ping_loopback(const uint8_t target[4], int count)
{
    net_ifconfig_t lo;
    if (net_ifconfig(0, &lo) < 0) {
        fprintf(stderr, "ping: no loopback interface\n");
        return 1;
    }
    memcpy(g_match_ip, target, 4);
    g_match_id = 0x1234;

    for (int n = 1; n <= count; n++) {
        uint8_t frame[NET_MAX_FRAME];
        int flen = build_ip_icmp(frame, lo.ip, target, g_match_id, (uint16_t)n);
        if (net_send("lo", frame, flen) < 0) {
            fprintf(stderr, "ping: send failed\n");
            return 1;
        }
        uint8_t buf[NET_MAX_FRAME];
        int blen = 0;
        if (!wait_match("lo", match_lo_reply, buf, &blen)) {
            printf("Request timeout for icmp_seq %d\n", n);
            continue;
        }
        printf("64 bytes from ");
        print_ip(target);
        printf(": icmp_seq=%d\n", n);
        usleep(1000000);
    }
    return 0;
}

static int ping_ethernet(const uint8_t target[4], int count)
{
    net_ifconfig_t eth;
    int idx = -1;
    for (int i = 0; i < 16; i++) {
        if (net_ifconfig(i, &eth) == 0 && strcmp(eth.name, "eth0") == 0) {
            idx = i;
            break;
        }
    }
    if (idx < 0) {
        fprintf(stderr, "ping: no eth0 interface\n");
        return 1;
    }

    uint8_t next_hop[4];
    uint8_t next_hop_mac[6];

    /* Route: on-subnet targets are sent directly; everything else goes to
     * the gateway (which NATs it out through the host). */
    int via_gateway = 0;
    if (ip_on_subnet(eth.ip, eth.netmask, target)) {
        memcpy(next_hop, target, 4);
    } else {
        if (ip_is_zero(eth.gw)) {
            fprintf(stderr, "ping: no route to host (no gateway)\n");
            return 1;
        }
        memcpy(next_hop, eth.gw, 4);
        via_gateway = 1;
    }

    printf("PING ");
    print_ip(target);
    printf(via_gateway ? " (via gateway " : " (");
    print_ip(next_hop);
    printf("): resolving MAC address...\n");

    if (!arp_resolve(&eth, next_hop, next_hop_mac)) {
        printf("Request timeout for icmp_seq %d\n", 1);
        fprintf(stderr, "ping: no ARP reply from ");
        print_ip(next_hop);
        printf("\n");
        return 1;
    }

    memcpy(g_match_ip, target, 4);
    g_match_id = 0x1234;

    for (int n = 1; n <= count; n++) {
        uint8_t frame[NET_MAX_FRAME];
        /* Ethernet goes to the next hop's MAC; the IP destination is the
         * target itself. */
        int flen = build_eth_icmp(frame, eth.mac, next_hop_mac,
                                  eth.ip, target, g_match_id, (uint16_t)n);
        if (net_send("eth0", frame, flen) < 0) {
            fprintf(stderr, "ping: send failed\n");
            return 1;
        }
        uint8_t buf[NET_MAX_FRAME];
        int blen = 0;
        if (!wait_match("eth0", match_eth_reply, buf, &blen)) {
            printf("Request timeout for icmp_seq %d\n", n);
            continue;
        }
        printf("64 bytes from ");
        print_ip(target);
        printf(": icmp_seq=%d\n", n);
        usleep(1000000);
    }
    return 0;
}

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "usage: ping <ip> [count]\n");
        return 1;
    }

    uint8_t target[4];
    if (parse_ip(argv[1], target) < 0) {
        fprintf(stderr, "ping: bad address '%s'\n", argv[1]);
        return 1;
    }

    int count = 4;
    if (argc > 2)
        count = atoi(argv[2]);
    if (count <= 0)
        count = 1;

    if (target[0] == 127)
        return ping_loopback(target, count);
    return ping_ethernet(target, count);
}

