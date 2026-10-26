/* Devices in the network: beacons by UDP broadcast, a table of the others, the list for the web page. */
#include "devices.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "lwip/udp.h"
#include "diag.h"

#define DEV_MAX     12                      /* other devices remembered */
#define DEV_JSON    420                     /* longest beacon (JSON) */
#define BEACON_MS   10000                   /* every 10 s (+-0.5 s, so devices do not send in step) */
#define FORGET_MS   35000                   /* gone after three missed beacons */

static struct { uint32_t ip, seen_ms; char json[DEV_JSON]; } s_dev[DEV_MAX];   /* lwIP context only */
static struct udp_pcb *s_pcb;
static volatile bool s_answer, s_ask;
static volatile uint32_t s_answer_at;
static uint32_t s_next_ms;

static uint32_t now_ms(void) { return to_ms_since_boot(get_absolute_time()); }
static uint32_t my_ip(void) { return ip4_addr_get_u32(netif_ip4_addr(&cyw43_state.netif[CYW43_ITF_STA])); }

/* foreign text goes into our JSON answer: one object, no control characters */
static bool json_ok(const char *s, int n) {
    if (n < 2 || s[0] != '{' || s[n - 1] != '}') return false;
    for (int i = 0; i < n; i++) if ((unsigned char)s[i] < 32) return false;
    return true;
}

static void recv_cb(void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *addr, u16_t port) {   /* lwIP context */
    (void)arg; (void)pcb; (void)port;
    static char buf[DEV_JSON + 16];
    uint16_t len = p->tot_len, n = pbuf_copy_partial(p, buf, sizeof buf - 1, 0);
    pbuf_free(p);
    uint32_t from = addr ? ip4_addr_get_u32(ip_2_ip4(addr)) : 0, now = now_ms();
    if (len != n || !from || from == my_ip()) return;
    if (n == 8 && !memcmp(buf, "C64DEV1?", 8)) {                     /* a query: answer soon, at a random moment */
        if (!s_answer) { s_answer_at = now + 50 + (uint32_t)(rand() % 350); s_answer = true; }
        return;
    }
    if (n < 10 || memcmp(buf, "C64DEV1 ", 8)) return;
    int jl = n - 8;
    while (jl > 0 && (buf[8 + jl - 1] == '\n' || buf[8 + jl - 1] == '\r')) jl--;
    if (jl >= DEV_JSON || !json_ok(buf + 8, jl)) return;
    int slot = -1, old = 0;
    for (int i = 0; i < DEV_MAX; i++) {
        if (s_dev[i].ip == from) { slot = i; break; }
        if (slot < 0 && !s_dev[i].ip) slot = i;
    }
    if (slot < 0) {                                                  /* full: the one not heard of for longest */
        for (int i = 1; i < DEV_MAX; i++) if (now - s_dev[i].seen_ms > now - s_dev[old].seen_ms) old = i;
        slot = old;
    }
    s_dev[slot].ip = from; s_dev[slot].seen_ms = now;
    memcpy(s_dev[slot].json, buf + 8, (size_t)jl); s_dev[slot].json[jl] = 0;
}

static void send_text(const char *s, int n) {
    cyw43_arch_lwip_begin();
    struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, (u16_t)n, PBUF_RAM);
    if (p) { memcpy(p->payload, s, (size_t)n); udp_sendto(s_pcb, p, IP_ADDR_BROADCAST, DEV_PORT); pbuf_free(p); }
    cyw43_arch_lwip_end();
}

void dev_start(void) {
    if (s_pcb) return;
    cyw43_arch_lwip_begin();
    s_pcb = udp_new_ip_type(IPADDR_TYPE_V4);
    if (s_pcb && udp_bind(s_pcb, IP_ANY_TYPE, DEV_PORT) == ERR_OK) udp_recv(s_pcb, recv_cb, NULL);
    else if (s_pcb) { udp_remove(s_pcb); s_pcb = NULL; }
    cyw43_arch_lwip_end();
    if (!s_pcb) { DIAG("geraete: UDP-Port %d nicht frei", DEV_PORT); return; }
    srand(my_ip() ^ time_us_32());
    s_ask = true;                                                    /* fill the list right away */
}

void dev_query(void) { s_ask = true; }

void dev_task(void) {
    if (!s_pcb) return;
    uint32_t now = now_ms();
    if (s_ask) {
        s_ask = false;
        send_text("C64DEV1?", 8);
        s_answer = true; s_answer_at = now + 20;                     /* and say who we are */
    }
    if ((int32_t)(now - s_next_ms) >= 0 || (s_answer && (int32_t)(now - s_answer_at) >= 0)) {
        s_answer = false;
        s_next_ms = now + BEACON_MS - 500 + (uint32_t)(rand() % 1000);
        static char b[DEV_JSON + 8];
        memcpy(b, "C64DEV1 ", 8);
        int n = net_self_json(b + 8, DEV_JSON);
        if (n > 0 && n < DEV_JSON) send_text(b, 8 + n);
    }
    cyw43_arch_lwip_begin();
    for (int i = 0; i < DEV_MAX; i++) if (s_dev[i].ip && now - s_dev[i].seen_ms > FORGET_MS) s_dev[i].ip = 0;
    cyw43_arch_lwip_end();
}

/* a JSON object as JSON string: " and \ escaped (control characters were refused on arrival) */
static int str_out(char *o, int n, const char *s) {
    int k = 0;
    if (k < n) o[k++] = '"';
    for (; *s && k < n - 3; s++) { if (*s == '"' || *s == '\\') o[k++] = '\\'; o[k++] = *s; }
    if (k < n) o[k++] = '"';
    return k;
}

int dev_json(char *o, int n) {                                       /* lwIP context (web route) */
    static char self[DEV_JSON];
    uint32_t now = now_ms();
    int k = snprintf(o, (size_t)n, "{\"list\":[{\"self\":true,\"age\":0,\"j\":");
    net_self_json(self, sizeof self);
    k += str_out(o + k, n - k, self);
    k += snprintf(o + k, (size_t)(n - k), "}");
    for (int i = 0; i < DEV_MAX && k < n - 2 * DEV_JSON - 64; i++) {   /* escaped: at most twice as long */
        if (!s_dev[i].ip) continue;
        k += snprintf(o + k, (size_t)(n - k), ",{\"age\":%lu,\"j\":", (unsigned long)((now - s_dev[i].seen_ms) / 1000));
        k += str_out(o + k, n - k, s_dev[i].json);
        k += snprintf(o + k, (size_t)(n - k), "}");
    }
    k += snprintf(o + k, (size_t)(n - k), "]}");
    return k < n ? k : n - 1;
}
