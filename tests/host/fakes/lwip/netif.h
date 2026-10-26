#pragma once
#include "lwip/ip4_addr.h"
struct netif { ip4_addr_t ip; const char *hostname; };
#define netif_set_hostname(n, name) ((n)->hostname = (name))
#define netif_ip4_addr(n) (&(n)->ip)
