#pragma once
#include "lwip/tcp.h"
struct udp_pcb { int port; };
#define PBUF_TRANSPORT 0
#define PBUF_RAM 0
struct pbuf *pbuf_alloc(int layer, u16_t len, int type);
err_t udp_sendto(struct udp_pcb *p, struct pbuf *b, const ip_addr_t *to, u16_t port);
struct udp_pcb *udp_new_ip_type(int t);
err_t udp_bind(struct udp_pcb *p, const ip_addr_t *a, u16_t port);
void udp_recv(struct udp_pcb *p, void (*f)(void*, struct udp_pcb*, struct pbuf*, const ip_addr_t*, u16_t), void *arg);
void udp_remove(struct udp_pcb *p);
/* every datagram sent, for the tests: port, receiver, length, the first bytes */
typedef struct { u16_t port; uint32_t to; int len; unsigned char d[800]; } udp_log_t;
extern udp_log_t UDP_LOG[256]; extern int UDP_LOG_N;
void test_udp_port(u16_t port, const uint8_t *pkt, int n, uint32_t from);   /* deliver to the pcb bound to port */
