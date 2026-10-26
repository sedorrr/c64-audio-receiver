#pragma once
#include <sys/types.h>
#include "lwip/netif.h"
struct eth_addr { uint8_t addr[6]; };
extern struct netif *netif_default;
/* fake ARP table: a test fills FAKE_ARP (ip -> mac) */
ssize_t etharp_find_addr(struct netif *netif, const ip4_addr_t *ipaddr, struct eth_addr **eth_ret, const ip4_addr_t **ip_ret);
