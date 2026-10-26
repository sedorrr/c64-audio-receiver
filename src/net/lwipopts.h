/* lwIP options for the C64 audio receiver (Pico 2 W): no OS, background IRQ processing via cyw43_arch. */
#ifndef _LWIPOPTS_H
#define _LWIPOPTS_H

#define NO_SYS                      1
#define LWIP_SOCKET                 0
#define LWIP_NETCONN                0
#define MEM_LIBC_MALLOC             0
#define MEM_ALIGNMENT               4
#define MEM_SIZE                    24000  /* forwarded stream packets are copied into RAM pbufs */
#define MEMP_NUM_TCP_SEG            32
#define MEMP_NUM_ARP_QUEUE          10
#define MEMP_NUM_TCP_PCB            16     /* web 4, REST 1, title relay 1, network search 6 */
#define MEMP_NUM_UDP_PCB            8      /* stream, mDNS, DNS, DHCP (client/server), device beacons */
#define PBUF_POOL_SIZE              40     /* audio stream: ~250 UDP packets per second, arriving in bursts */
#define LWIP_ARP                    1
#define ARP_TABLE_SIZE              32     /* network search: 6 addresses asked at once, the default 10 evicted answers */
#define LWIP_ETHERNET               1
#define LWIP_ICMP                   1
#define LWIP_RAW                    1
#define TCP_MSS                     1460
#define TCP_WND                     (8 * TCP_MSS)
#define TCP_SND_BUF                 (8 * TCP_MSS)
#define TCP_SND_QUEUELEN            ((4 * (TCP_SND_BUF) + (TCP_MSS - 1)) / (TCP_MSS))
#define LWIP_NETIF_STATUS_CALLBACK  1
#define LWIP_NETIF_LINK_CALLBACK    1
#define LWIP_NETIF_HOSTNAME         1
#define LWIP_CHKSUM_ALGORITHM       3
#define LWIP_DHCP                   1
#define LWIP_IGMP                   1      /* mDNS (224.0.0.251) */
#define LWIP_IPV4                   1
#define LWIP_TCP                    1
#define LWIP_UDP                    1
#define LWIP_DNS                    1      /* addresses may be names: ultimate.fritz.box, c64viz.local */
#define LWIP_DNS_SUPPORT_MDNS_QUERIES 1    /* "<name>.local" is asked by multicast (the visualizer answers) */
#define MEMP_NUM_SYS_TIMEOUT        (LWIP_NUM_SYS_TIMEOUT_INTERNAL + 8)
#define LWIP_TCP_KEEPALIVE          1
#define LWIP_NETIF_TX_SINGLE_PBUF   1
#define DHCP_DOES_ARP_CHECK         0
#define LWIP_DHCP_DOES_ACD_CHECK    0
#define LWIP_STATS                  0
#define LWIP_DEBUG                  0

#endif
