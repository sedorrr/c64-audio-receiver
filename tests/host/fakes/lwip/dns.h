#pragma once
#include "lwip/tcp.h"
#define ERR_INPROGRESS -5
typedef void (*dns_found_callback)(const char *name, const ip_addr_t *ipaddr, void *arg);
err_t dns_gethostbyname(const char *hostname, ip_addr_t *addr, dns_found_callback found, void *arg);
