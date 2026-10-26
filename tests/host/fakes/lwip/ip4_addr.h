#pragma once
#include <stdint.h>
typedef struct { uint32_t addr; } ip4_addr_t;
typedef ip4_addr_t ip_addr_t;
#define IP4_ADDR(a,b,c,d,e) ((a)->addr = ((uint32_t)(b)) | ((uint32_t)(c)<<8) | ((uint32_t)(d)<<16) | ((uint32_t)(e)<<24))
#define ip_2_ip4(x) (x)
int ipaddr_aton(const char *s, ip_addr_t *a);
char *ip4addr_ntoa(const ip4_addr_t *a);
#define ipaddr_ntoa ip4addr_ntoa
#define ip4_addr1(a) ((uint8_t)((a)->addr))
#define ip4_addr2(a) ((uint8_t)((a)->addr >> 8))
#define ip4_addr3(a) ((uint8_t)((a)->addr >> 16))
#define ip4_addr4(a) ((uint8_t)((a)->addr >> 24))
#define IPADDR_TYPE_ANY 46
#define IPADDR_TYPE_V4 0
extern ip_addr_t IP_ANY_FAKE;
#define IP_ANY_TYPE (&IP_ANY_FAKE)
#define ip_addr_copy(d,s) ((d)=(s))
#define ip4_addr_get_u32(a) ((a)->addr)
#define ip4_addr_set_u32(a, v) ((a)->addr = (v))
extern const ip_addr_t IP_BCAST_FAKE;
#define IP_ADDR_BROADCAST (&IP_BCAST_FAKE)
