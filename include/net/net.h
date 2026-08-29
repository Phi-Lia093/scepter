#ifndef NET_NET_H
#define NET_NET_H

#include <stdint.h>
#include <stddef.h>
#include "kernel/sched.h"   /* wait_queue_head_t, wake_up, sleep_on */

/* =========================================================================
 * Networking Core
 *
 * A small network subsystem consisting of:
 *   - an interface registry (loopback, ethernet NICs)
 *   - a per-interface receive queue of packet buffers
 *   - a raw-frame transmit path
 *   - a minimal in-kernel protocol layer (ARP + IPv4/ICMP echo) that
 *     answers pings; everything else is queued for userspace.
 *
 * The driver-facing API is net_rx()/net_transmit(); user-facing access
 * goes through the SYS_NET_* syscalls (kernel/syscalls_net.c).
 * ========================================================================= */

/* Maximum frame size carried through the subsystem. */
#define NET_MAX_FRAME       2048
#define NET_MAC_LEN         6
#define NET_NAME_LEN        16

/* Interface types */
#define NET_IFACE_LOOPBACK  1
#define NET_IFACE_ETHERNET  2

/* Interface flags */
#define NET_IF_UP           0x0001
#define NET_IF_LOOPBACK     0x0002

/* Default MTUs */
#define NET_MTU_ETHERNET    1500
#define NET_MTU_LOOPBACK    65536

/* =========================================================================
 * Statistics
 * ========================================================================= */

typedef struct {
    uint32_t rx_packets;
    uint32_t tx_packets;
    uint32_t rx_bytes;
    uint32_t tx_bytes;
    uint32_t rx_dropped;
    uint32_t tx_dropped;
} net_stats_t;

/* =========================================================================
 * Interface abstraction
 * ========================================================================= */

struct net_iface;

/* Driver transmit callback: hand a full frame to the NIC.
 * Returns 0 on success, -1 on failure. */
typedef int (*net_transmit_fn)(struct net_iface *iface,
                               const uint8_t *data, uint32_t len);

/* One queued receive packet (taken from a static pool - never kalloc in
 * interrupt context). */
typedef struct net_pkt {
    struct net_pkt *next;
    struct net_iface *iface;
    uint32_t        len;
    uint8_t         data[NET_MAX_FRAME];
} net_pkt_t;

typedef struct net_iface {
    char            name[NET_NAME_LEN];
    uint8_t         mac[NET_MAC_LEN];
    uint8_t         ip[4];
    uint8_t         netmask[4];
    uint8_t         gw[4];
    uint32_t        mtu;
    uint32_t        type;
    uint32_t        flags;
    net_stats_t     stats;
    net_transmit_fn transmit;
    net_pkt_t      *rx_head;   /* pending packets for userspace */
    net_pkt_t      *rx_tail;
    uint32_t        rx_count;
    wait_queue_head_t rx_wq;   /* blocking net_recv waiters */
} net_iface_t;

/* =========================================================================
 * Userspace ABI: net_ifconfig
 *
 * Shared with crt/include/sys/net.h.  _pad keeps mtu 4-byte aligned so the
 * layout is fully deterministic for both kernel and userspace.
 * ========================================================================= */

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

/* =========================================================================
 * Core API
 * ========================================================================= */

/** Initialize the networking core: packet pool + loopback interface. */
void net_init(void);

/** Register an interface (static array, allocation-free). Returns 0/-1. */
int  net_register_iface(net_iface_t *iface);

/** Look up an interface by name ("lo", "eth0", ...). NULL if absent. */
net_iface_t *net_find_iface(const char *name);

/** Look up an interface by registry index (0 = first registered). */
net_iface_t *net_get_iface(int index);

/** Send a frame on an interface. 0 on success, -1 on failure. */
int  net_transmit(net_iface_t *iface, const uint8_t *data, uint32_t len);

/**
 * Deliver a received frame to the network subsystem (call from the driver
 * IRQ handler).  The frame is first offered to the in-kernel protocol
 * layer (ARP/ICMP); if not consumed it is queued for userspace.
 * Safe to call from interrupt context (no allocation; static pool).
 */
void net_rx(net_iface_t *iface, const uint8_t *data, uint32_t len);

/** 1 if a frame is pending on the interface's RX queue. */
int  net_poll(net_iface_t *iface);

/** Dequeue one frame into buf. Returns bytes copied, 0 if queue empty. */
int  net_recv(net_iface_t *iface, uint8_t *buf, uint32_t buflen);

/** Change the interface's static IPv4 configuration. */
void net_set_ip(net_iface_t *iface, const uint8_t ip[4],
                const uint8_t netmask[4], const uint8_t gw[4]);

/* =========================================================================
 * Protocol layer (net/proto.c) - ARP + IPv4/ICMP echo handling
 * ========================================================================= */

/**
 * Offer a received frame to the protocol layer.
 * Returns 1 if consumed (an ARP/ICMP reply was generated in-kernel),
 * 0 if the frame should be forwarded to the userspace queue.
 */
int  net_proto_rx(net_iface_t *iface, const uint8_t *data, uint32_t len);

#endif /* NET_NET_H */
