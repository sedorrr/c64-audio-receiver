#pragma once
#include "lwip/netif.h"
typedef struct { int x; } dhcp_server_t;
static inline void dhcp_server_init(dhcp_server_t *d, struct netif *n, ip_addr_t *ip, ip_addr_t *nm){(void)d;(void)n;(void)ip;(void)nm;}
static inline void dhcp_server_deinit(dhcp_server_t *d){(void)d;}
