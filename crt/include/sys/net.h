#ifndef _SYS_NET_H
#define _SYS_NET_H

#include <stdint.h>

/* =========================================================================
 * Networking syscalls exposed to userspace (custom SYS_NET_* numbers).
 *
 * The net_ifconfig_t layout MUST match include/net/net.h in the kernel.
 * The _pad field keeps mtu 4-byte aligned so the layout is deterministic.
 * ========================================================================= */

#define NET_NAME_LEN  16
#define NET_MAC_LEN   6
#define NET_MAX_FRAME 2048

#define NET_IF_UP       0x0001
#define NET_IF_LOOPBACK 0x0002

typedef struct net_ifconfig {
    char     name[NET_NAME_LEN];
    uint8_t  mac[NET_MAC_LEN];
    uint8_t  ip[4];
    uint8_t  netmask[4];
    uint8_t  gw[4];
    uint8_t  _pad[2];
    uint32_t mtu;
    uint32_t flags;
    uint32_t rx_packets;
    uint32_t tx_packets;
    uint32_t rx_bytes;
    uint32_t tx_bytes;
    uint32_t rx_dropped;
    uint32_t tx_dropped;
} net_ifconfig_t;

/* Query interface i (0, 1, 2, ...). Returns 0 on success, -1 on error
 * (errno = ENODEV when there are no more interfaces). */
int net_ifconfig(int index, net_ifconfig_t *out);

/* Transmit a raw frame on an interface ("lo", "eth0"). Returns bytes sent
 * or -1 on error. */
int net_send(const char *name, const void *frame, unsigned int len);

/* Receive the next raw frame from an interface.  When block is non-zero the
 * call waits for a frame; otherwise returns 0 immediately if none is
 * pending.  Returns bytes received or -1 on error. */
int net_recv(const char *name, void *buf, unsigned int buflen, int block);

/* Set an interface's static IPv4 configuration. Returns 0 or -1. */
int net_setip(const char *name, const void *ip,
              const void *netmask, const void *gw);

#endif /* _SYS_NET_H */
