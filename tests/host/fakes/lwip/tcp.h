#pragma once
#include <stdint.h>
#include <stddef.h>
#include "lwip/ip4_addr.h"
typedef int8_t err_t; typedef uint16_t u16_t; typedef uint8_t u8_t;
#define ERR_OK 0
#define ERR_VAL -6
#define ERR_MEM -1
#define ERR_ABRT -13
#define TCP_WRITE_FLAG_COPY 1
#define TCP_WRITE_FLAG_MORE 2
struct pbuf { struct pbuf *next; void *payload; uint16_t tot_len, len; };
struct tcp_pcb { void *arg; int closed; char out[70000]; int outlen; void *recv, *sent, *err; int port; int seen; ip_addr_t remote_ip; uint16_t rcv_wnd, rcv_ann_wnd; };
u16_t pbuf_copy_partial(const struct pbuf *p, void *d, u16_t len, u16_t off);
void pbuf_free(struct pbuf *p);
struct tcp_pcb *tcp_new_ip_type(int t);
err_t tcp_bind(struct tcp_pcb *p, const ip_addr_t *a, u16_t port);
struct tcp_pcb *tcp_listen_with_backlog(struct tcp_pcb *p, int b);
void tcp_accept(struct tcp_pcb *p, err_t (*f)(void*, struct tcp_pcb*, err_t));
void tcp_arg(struct tcp_pcb *p, void *a);
void tcp_recv(struct tcp_pcb *p, err_t (*f)(void*, struct tcp_pcb*, struct pbuf*, err_t));
void tcp_sent(struct tcp_pcb *p, err_t (*f)(void*, struct tcp_pcb*, u16_t));
void tcp_err(struct tcp_pcb *p, void (*f)(void*, err_t));
void tcp_poll(struct tcp_pcb *p, err_t (*f)(void*, struct tcp_pcb*), u8_t iv);
err_t tcp_write(struct tcp_pcb *p, const void *d, u16_t n, u8_t f);
err_t tcp_output(struct tcp_pcb *p);
int tcp_sndbuf(struct tcp_pcb *p);
int tcp_sndqueuelen(struct tcp_pcb *p);
void tcp_recved(struct tcp_pcb *p, u16_t n);
err_t tcp_close(struct tcp_pcb *p);
void tcp_abort(struct tcp_pcb *p);
err_t tcp_connect(struct tcp_pcb *p, const ip_addr_t *a, u16_t port, err_t (*f)(void*, struct tcp_pcb*, err_t));
