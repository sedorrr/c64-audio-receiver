#pragma once
#include <string.h>
#include "lwip/netif.h"
#include "lwip/tcp.h"
extern char JOINED[32];
static inline err_t igmp_joingroup_netif(struct netif *n, const ip4_addr_t *g){ (void)n; char *ip4addr_ntoa(const ip4_addr_t *a); extern char JOINED[32]; strcpy(JOINED, ip4addr_ntoa(g)); return 0; }
int ip4addr_aton(const char *s, ip4_addr_t *a);
