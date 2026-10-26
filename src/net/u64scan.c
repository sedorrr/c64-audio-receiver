/* Search the home network for Ultimate devices - see u64scan.h.
   Every address gets a TCP connection to port 80 (a missing device: no ARP answer, given up after 0.7 s), then
   "GET /v1/info". The lwIP callbacks only copy the answer; checking and closing happens in u64scan_task. */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "lwip/tcp.h"
#include "lwip/etharp.h"
#include "u64scan.h"
#include "diag.h"

#ifndef U64SCAN_PAR                                   /* connections at a time (the Pico has 16 TCP slots); the audio receiver */
#define U64SCAN_PAR 6                                  /* uses 3: every address costs ~5 ARP broadcasts, 6 at a time lost audio */
#endif
#define PAR        U64SCAN_PAR
#define CONNECT_MS 900                                 /* no answer to the connection: nobody there (WLAN devices need ~0.7 s) */
#define ANSWER_MS  2500                                /* the Ultimate's web server is slow */
#define BUF        1024

typedef struct {
    struct tcp_pcb *pcb;
    uint32_t ip;
    absolute_time_t deadline;
    volatile bool used, connected, done;
    bool waiting;                                      /* connected: waiting for the answer (main loop only) */
    char mac[18];                                      /* from the ARP table at connect time (it is small: later the entry may be gone) */
    volatile int len;
    char buf[BUF];
} slot_t;
static slot_t *s_slot = NULL;                         /* only while searching (6 KB from the heap) */
/* one entry per device: an Ultimate with Ethernet and WLAN answers on two addresses with the same unique_id.
   The Ethernet one is preferred (the stream would otherwise cross the WLAN twice), the other is kept as alt. */
static struct { uint32_t ip, alt; char name[33], product[33], uid[17]; int8_t wired; } s_found[U64SCAN_MAX_FOUND];
static int s_nfound = 0, s_next = 0, s_done = 0;       /* s_next: host part 1..254 still to start */
static uint32_t s_base = 0, s_me = 0;
static bool s_running = false;

bool u64scan_running(void) { return s_running; }

void u64scan_start(uint32_t my_ip) {
    if (s_running || !my_ip) return;
    if (!s_slot && !(s_slot = calloc(PAR, sizeof *s_slot))) return;
    s_base = my_ip & 0x00FFFFFFu; s_me = my_ip;
    s_nfound = 0; s_next = 1; s_done = 0; s_running = true;
}

/* ---- lwIP context: only copy */
static void cb_err(void *arg, err_t e) {               /* the pcb is already freed by lwIP */
    (void)e; slot_t *s = (slot_t *)arg;
    if (!s) return;
    s->pcb = NULL; s->done = true;
}
static err_t cb_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t e) {
    (void)e; slot_t *s = (slot_t *)arg;
    if (!s) { if (p) pbuf_free(p); return ERR_OK; }
    if (!p) { s->done = true; return ERR_OK; }         /* answer complete (closed by the other side) */
    int room = BUF - 1 - s->len;
    if (room > 0) s->len += pbuf_copy_partial(p, s->buf + s->len, (u16_t)(p->tot_len < room ? p->tot_len : room), 0);
    tcp_recved(pcb, p->tot_len);
    pbuf_free(p);
    if (s->len >= BUF - 1) s->done = true;
    return ERR_OK;
}
static err_t cb_connected(void *arg, struct tcp_pcb *pcb, err_t e) {
    slot_t *s = (slot_t *)arg;
    if (!s || e != ERR_OK) return ERR_OK;
    char rq[96];
    int n = snprintf(rq, sizeof rq, "GET /v1/info HTTP/1.1\r\nHost: %lu.%lu.%lu.%lu\r\nConnection: close\r\n\r\n",
                     (unsigned long)(s->ip & 255), (unsigned long)((s->ip >> 8) & 255), (unsigned long)((s->ip >> 16) & 255),
                     (unsigned long)(s->ip >> 24));
    struct eth_addr *m = NULL; const ip4_addr_t *f = NULL; ip4_addr_t a; ip4_addr_set_u32(&a, s->ip);
    if (etharp_find_addr(netif_default, &a, &m, &f) >= 0 && m)
        snprintf(s->mac, sizeof s->mac, "%02x:%02x:%02x:%02x:%02x:%02x", m->addr[0], m->addr[1], m->addr[2], m->addr[3], m->addr[4], m->addr[5]);
    /* small receive window (instead of TCP_WND = 8 segments): an answer of up to 1 KB is all we read, and a device
       that sends a big page at once (a NAS, a router) would otherwise fill the pbuf pool - with 6 connections that
       crowded out the audio stream (lost packets, found on the audio receiver). Set before our ACK goes out. */
    pcb->rcv_wnd = pcb->rcv_ann_wnd = BUF;
    tcp_write(pcb, rq, (u16_t)n, TCP_WRITE_FLAG_COPY);
    tcp_output(pcb);
    s->connected = true;
    return ERR_OK;
}

/* ---- main loop */
static void json_str(const char *body, const char *key, char *out, int n) {   /* "key": "value" -> value */
    out[0] = 0;
    char k[24]; snprintf(k, sizeof k, "\"%s\"", key);
    const char *p = strstr(body, k);
    if (!p) return;
    p += strlen(k);
    while (*p == ' ' || *p == ':' || *p == '\t') p++;
    if (*p++ != '"') return;
    int i = 0;
    while (*p && *p != '"' && i < n - 1) {
        char c = *p++;
        if (c == '\\' && *p) c = *p++;
        out[i++] = (c < ' ' || c == '"' || c == '\\') ? ' ' : c;   /* safe to put into JSON again */
    }
    out[i] = 0;
}
/* Ethernet or WLAN? The Ultimate 64 tells its wifi_mac; otherwise its Ethernet MACs start with 02:15:41 (locally
   administered), the WLAN modules have a maker's MAC. 1 = Ethernet, 0 = WLAN, -1 = unknown */
static int wired(const char *m, const char *wifi_mac) {
    if (!m[0]) return -1;
    if (wifi_mac[0]) { bool same = true; for (int k = 0; k < 17 && same; k++) same = (m[k] | 32) == (wifi_mac[k] | 32); return same ? 0 : 1; }
    return !strncmp(m, "02:15:41", 8) ? 1 : 0;
}
static bool has_ultimate(const char *s) {
    for (; *s; s++) if ((s[0] | 32) == 'u' && !strncmp(s + 1, "ltimate", 7)) return true;
    return false;
}
static void finish(slot_t *s) {
    if (s->pcb) {                                      /* reset instead of close: no TIME_WAIT slot left behind */
        cyw43_arch_lwip_begin();
        tcp_arg(s->pcb, NULL); tcp_err(s->pcb, NULL); tcp_recv(s->pcb, NULL);
        tcp_abort(s->pcb);
        cyw43_arch_lwip_end();
        s->pcb = NULL;
    }
    s->buf[s->len] = 0;
    const char *body = strstr(s->buf, "\r\n\r\n");
    if (body && (!strncmp(s->buf, "HTTP/1.1 200", 12) || !strncmp(s->buf, "HTTP/1.0 200", 12)) && s_nfound < U64SCAN_MAX_FOUND) {
        char product[33]; json_str(body, "product", product, sizeof product);
        if (has_ultimate(product)) {
            char uid[17], wmac[24];
            json_str(body, "unique_id", uid, sizeof uid); json_str(body, "wifi_mac", wmac, sizeof wmac);
            if (!s->mac[0]) {                              /* not seen at connect time: the ARP table once more */
                struct eth_addr *m = NULL; const ip4_addr_t *f = NULL; ip4_addr_t a; ip4_addr_set_u32(&a, s->ip);
                cyw43_arch_lwip_begin();
                if (etharp_find_addr(netif_default, &a, &m, &f) >= 0 && m)
                    snprintf(s->mac, sizeof s->mac, "%02x:%02x:%02x:%02x:%02x:%02x", m->addr[0], m->addr[1], m->addr[2], m->addr[3], m->addr[4], m->addr[5]);
                cyw43_arch_lwip_end();
            }
            int w = wired(s->mac, wmac), k = 0;
            DIAG("scan: %lu.%lu.%lu.%lu %s id=%s mac=%s -> %s", (unsigned long)(s->ip & 255), (unsigned long)((s->ip >> 8) & 255),
                 (unsigned long)((s->ip >> 16) & 255), (unsigned long)(s->ip >> 24), product, uid, s->mac[0] ? s->mac : "?",
                 w == 1 ? "Ethernet" : (w == 0 ? "WLAN" : "unbekannt"));
            while (k < s_nfound && !(uid[0] && !strcmp(s_found[k].uid, uid))) k++;
            if (k < s_nfound) {                            /* the same device on its second address */
                /* Ethernet before unknown before WLAN */
                int rank_new = w == 1 ? 2 : (w < 0 ? 1 : 0), old = s_found[k].wired, rank_old = old == 1 ? 2 : (old < 0 ? 1 : 0);
                if (rank_new > rank_old) { s_found[k].alt = s_found[k].ip; s_found[k].ip = s->ip; s_found[k].wired = (int8_t)w; }
                else s_found[k].alt = s->ip;
            } else {
                s_found[k].ip = s->ip; s_found[k].alt = 0; s_found[k].wired = (int8_t)w;
                strcpy(s_found[k].product, product); strcpy(s_found[k].uid, uid);
                json_str(body, "hostname", s_found[k].name, sizeof s_found[k].name);
                s_nfound++;
            }
        }
    }
    s->used = false; s_done++;
}
void u64scan_task(void) {
    if (!s_running) return;
    absolute_time_t now = get_absolute_time();
    for (int i = 0; i < PAR; i++) {
        slot_t *s = &s_slot[i];
        if (s->used) {
            if (s->connected && !s->waiting) { s->waiting = true; s->deadline = delayed_by_ms(now, ANSWER_MS); }   /* someone is there */
            if (s->done || time_reached(s->deadline)) finish(s);
            continue;
        }
        while (s_next <= 254 && ((s_base | (uint32_t)s_next << 24) == s_me)) { s_next++; s_done++; }   /* not ourselves */
        if (s_next > 254) continue;
        uint32_t ip = s_base | (uint32_t)s_next << 24;
        cyw43_arch_lwip_begin();
        struct tcp_pcb *pcb = tcp_new_ip_type(IPADDR_TYPE_V4);
        if (pcb) {
            memset(s, 0, sizeof *s);
            s->used = true; s->ip = ip; s->pcb = pcb; s->deadline = delayed_by_ms(now, CONNECT_MS);
            tcp_arg(pcb, s); tcp_err(pcb, cb_err); tcp_recv(pcb, cb_recv);
            ip_addr_t a; ip4_addr_set_u32(ip_2_ip4(&a), ip);
            if (tcp_connect(pcb, &a, 80, cb_connected) != ERR_OK) { tcp_arg(pcb, NULL); tcp_err(pcb, NULL); tcp_abort(pcb); s->pcb = NULL; s->done = true; }
            s_next++;
        }
        cyw43_arch_lwip_end();
        if (!pcb) break;                                /* no free TCP slot right now: try again next round */
    }
    if (s_next > 254) {
        bool busy = false;
        for (int i = 0; i < PAR; i++) busy |= s_slot[i].used;
        if (!busy) { s_running = false; free(s_slot); s_slot = NULL; }
    }
}

int u64scan_json(char *out, int n) {
    int k = snprintf(out, (size_t)n, "{\"running\":%s,\"done\":%d,\"total\":254,\"found\":[", s_running ? "true" : "false", s_done);
    for (int i = 0; i < s_nfound && k < n - 160; i++) {
        char ip[16], alt[16] = "";
        uint32_t a = s_found[i].ip, b = s_found[i].alt;
        snprintf(ip, sizeof ip, "%lu.%lu.%lu.%lu", (unsigned long)(a & 255), (unsigned long)((a >> 8) & 255), (unsigned long)((a >> 16) & 255), (unsigned long)(a >> 24));
        if (b) snprintf(alt, sizeof alt, "%lu.%lu.%lu.%lu", (unsigned long)(b & 255), (unsigned long)((b >> 8) & 255), (unsigned long)((b >> 16) & 255), (unsigned long)(b >> 24));
        k += snprintf(out + k, (size_t)(n - k), "%s{\"ip\":\"%s\",\"alt\":\"%s\",\"link\":\"%s\",\"name\":\"%s\",\"product\":\"%s\"}", i ? "," : "",
                      ip, alt, s_found[i].wired == 1 ? "eth" : (s_found[i].wired == 0 ? "wlan" : ""), s_found[i].name, s_found[i].product);
    }
    k += snprintf(out + k, (size_t)(n - k), "]}");
    return k < n ? k : n - 1;
}
