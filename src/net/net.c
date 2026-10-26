/*
 * WLAN for the audio receiver (Pico 2 W)
 *  - connects to the home WLAN; without settings (or if that fails) it opens its own access point
 *    "C64-Audio" (password "commodore64", page at http://192.168.4.1)
 *  - small web server with Digest login: playback, source, volume, settings, status, firmware update
 *  - asks the chosen Ultimate for its audio stream (PUT /v1/streams/audio:start?ip=<this Pico>:11001) and
 *    stops it again (audio:stop); several Ultimates can be stored, one is the source
 *  - receives the stream (UDP 11001), hands every packet to the player and forwards it unchanged to the
 *    visualizer (the Ultimate streams to one receiver only)
 * Large parts come from the C64 visualizer (net.c there): web server, Digest login, REST client, settings.
 *
 * lwIP runs in the background (pico_cyw43_arch_lwip_threadsafe_background). Its callbacks only copy data
 * and set flags; flash writes happen in net_task() on the main loop.
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "lwip/tcp.h"
#include "lwip/udp.h"
#include "lwip/dns.h"
#include "lwip/ip4_addr.h"
#include "lwip/netif.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "hardware/watchdog.h"
#include "hardware/regs/addressmap.h"
#include "pico/rand.h"
#include "dhcpserver.h"
#include "mdns_lite.h"
#include "md5.h"
#include "diag.h"
#include "ota.h"
#include "config.h"
#include "net.h"
#include "net_internal.h"
#include "web_page.h"
#include "help_page.h"
#include "player.h"
#include "prefs.h"
#include "audio/u64stream.h"
#include "np_relay.h"
#include "u64scan.h"
#include "devices.h"

#define AP_SSID        "C64-Audio"
#define AP_PASS        "commodore64"
#define WEB_PW_DEFAULT "commodore64"                     /* password of the web page until it is changed */
#define REALM          "C64 Audio"
#define CONNECT_TIMEOUT_MS 20000                         /* one connection attempt (until logged in at the router) */
#define DHCP_TIMEOUT_MS    30000                         /* logged in, waiting for an address (some routers are slow) */
#define CONNECT_TRIES      3                             /* attempts before the own access point (first connect only) */
#define NETCFG_MAGIC_V1 0xA0D1C001u                     /* first layout, without the visualizer's password */
#define NETCFG_MAGIC   0xA0D1C002u
#define VIZ_MAGIC_V3   0xC64E7C03u                       /* the visualizer's settings in the same place */
#define BODY_MAX       (36 * 1024)                       /* POST bodies: settings, firmware chunks (32 KB) */
#define REQ_RETRY_MS   10000                             /* ask for the stream again while none arrives */
#define REQ_RETRY_CONTESTED_MS 60000                     /* ... when another device keeps taking it */
#define RX_TIMEOUT_US  1000000u                          /* no packet for 1 s = no stream */

/* ------------------------------------------------------------ stored settings */
typedef struct { char label[17]; char addr[64]; char pw[33]; } u64cfg_t;
typedef struct {
    uint32_t magic;
    char     ssid[33], pass[65], webpw[33];
    char     name[MDNSL_NAME_MAX + 1];                    /* device name in the network ("" = c64audio) */
    u64cfg_t u64[U64_MAX];                                /* address = IP or name (e.g. ultimate.fritz.box) */
    char     fwd[64];                                     /* forward the stream to (IP or name, e.g. c64viz.local) */
    uint8_t  fwd_on;
    uint8_t  fed;          /* the stream comes forwarded (Pi jukebox, another receiver): never ask the Ultimate itself -
                              else it takes the stream away from the forwarder whenever that one stalls (2026-10-23).
                              The former pad byte: 0 in every stored layout = asks as before */
    uint16_t latency_ms;
    char     fwdpw[33];                                   /* web password of the visualizer: title messages of the PC jukebox */
    uint32_t check;
} netcfg_t;
typedef struct {                                         /* layout v1 (2026-10-14): migrated */
    uint32_t magic;
    char     ssid[33], pass[65], webpw[33];
    char     name[MDNSL_NAME_MAX + 1];
    u64cfg_t u64[U64_MAX];
    char     fwd[64];
    uint8_t  fwd_on, pad;
    uint16_t latency_ms;
    uint32_t check;
} netcfg_v1_t;
_Static_assert(sizeof(netcfg_t) <= FLASH_SECTOR_SIZE, "settings must fit into one flash sector");

typedef struct {                                         /* visualizer layout v3: its WLAN settings are taken over */
    uint32_t magic;
    char     ssid[33], pass[65], u64[64], u64pw[33], webpw[33];
    char     name[MDNSL_NAME_MAX + 1];
    uint8_t  stream, smode, pad;
    uint32_t check;
} viz_cfg_t;

static netcfg_t g_cfg;
static prefs_t  g_prefs;

static uint32_t fnv(const void *data, size_t len) {
    const uint8_t *p = (const uint8_t *)data;
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < len; i++) { h ^= p[i]; h *= 16777619u; }
    return h;
}
static uint32_t cfg_check(const netcfg_t *c) { return fnv(c, offsetof(netcfg_t, check)); }

static void cfg_load(void) {
    const void *flash = (const void *)(XIP_BASE + NETCFG_OFFSET);
    memcpy(&g_cfg, flash, sizeof g_cfg);
    netcfg_v1_t o;
    memcpy(&o, flash, sizeof o);
    if (g_cfg.magic == NETCFG_MAGIC && g_cfg.check == cfg_check(&g_cfg)) {
        /* current layout */
    } else if (o.magic == NETCFG_MAGIC_V1 && o.check == fnv(&o, offsetof(netcfg_v1_t, check))) {
        memset(&g_cfg, 0, sizeof g_cfg);                  /* everything stays, the visualizer's password is new */
        memcpy(&g_cfg, &o, offsetof(netcfg_v1_t, check));
    } else {
        viz_cfg_t v;
        memcpy(&v, flash, sizeof v);
        memset(&g_cfg, 0, sizeof g_cfg);
        g_cfg.latency_ms = LATENCY_DEFAULT_MS;
        if (v.magic == VIZ_MAGIC_V3 && v.check == fnv(&v, offsetof(viz_cfg_t, check))) {
            /* this Pico ran the visualizer before: keep its WLAN, web password and Ultimate */
            memcpy(g_cfg.ssid, v.ssid, sizeof v.ssid); memcpy(g_cfg.pass, v.pass, sizeof v.pass);
            memcpy(g_cfg.webpw, v.webpw, sizeof v.webpw);
            memcpy(g_cfg.u64[0].addr, v.u64, sizeof v.u64); memcpy(g_cfg.u64[0].pw, v.u64pw, sizeof v.u64pw);
            DIAG("net: WLAN-Einstellungen des Visualizers übernommen");
        }
    }
    g_cfg.ssid[32] = g_cfg.pass[64] = g_cfg.webpw[32] = g_cfg.fwd[63] = g_cfg.fwdpw[32] = 0;
    for (int i = 0; i < U64_MAX; i++) { g_cfg.u64[i].label[16] = g_cfg.u64[i].addr[63] = g_cfg.u64[i].pw[32] = 0; }
    g_cfg.name[MDNSL_NAME_MAX] = 0;
    if (!mdnsl_name_valid(g_cfg.name)) g_cfg.name[0] = 0;
    if (g_cfg.latency_ms < LATENCY_MIN_MS || g_cfg.latency_ms > LATENCY_MAX_MS) g_cfg.latency_ms = LATENCY_DEFAULT_MS;
}
static const char *base_name(void) { return g_cfg.name[0] ? g_cfg.name : DEVICE_NAME_DEFAULT; }
static const char *web_password(void) { return g_cfg.webpw[0] ? g_cfg.webpw : WEB_PW_DEFAULT; }
static char g_ap_ssid[33];                               /* "C64-Audio" or "C64-Audio-<name>" */
static char g_hostname[MDNSL_NAME_MAX + 1];

/* erase + program a flash region; main loop only. The player queues ~60 ms of audio first, the sector erase
   (~45 ms) stops all interrupts. */
static void flash_write(uint32_t offset, const uint8_t *data, uint32_t len) {
    static uint8_t page[FLASH_PAGE_SIZE];
    for (uint32_t s = 0; s < len; s += FLASH_SECTOR_SIZE) {
        watchdog_update();
        uint32_t irq = player_flash_begin(true);
        flash_range_erase(offset + s, FLASH_SECTOR_SIZE);
        player_flash_end(irq);
    }
    for (uint32_t pos = 0; pos < len; pos += FLASH_PAGE_SIZE) {
        uint32_t n = len - pos < FLASH_PAGE_SIZE ? len - pos : FLASH_PAGE_SIZE;
        memset(page, 0xFF, sizeof page);
        memcpy(page, data + pos, n);
        uint32_t irq = save_and_disable_interrupts();
        flash_range_program(offset + pos, page, FLASH_PAGE_SIZE);
        restore_interrupts(irq);
    }
}

/* ------------------------------------------------------------ state */
typedef enum { NS_OFF, NS_CONNECTING, NS_STA, NS_AP } nstate_t;
static nstate_t g_state = NS_OFF;
static absolute_time_t g_t_connect, g_t_pps, g_t_req, g_t_prefs;
static bool g_prefs_dirty = false;
static char g_ip[16] = "-";
static dhcp_server_t g_dhcp;
static bool g_servers_up = false;
static bool g_state_was_sta = false;                     /* reconnecting after a lost link: no access point fallback */
static char g_body[BODY_MAX];
static netcfg_t g_newcfg;
static volatile bool g_pending_cfg = false;
static int g_newsrc = -1, g_newplay = -1;                /* with the settings (setup assistant): source, on/off; -1 = keep */
static netcfg_t g_restcfg;                               /* a backup being restored (checked by the route) */
static prefs_t  g_restprefs;
static bool     g_rest_has_prefs;
static volatile bool g_pending_restore = false;

void net_post_message(const char *m) { DIAG("%s", m); }
char *net_body_buffer(void) { return g_body; }

/* ------------------------------------------------------------ address lookup (IP, DNS name or <name>.local) */
typedef struct { char host[64]; ip_addr_t ip; volatile bool have, busy, failed; absolute_time_t next; } res_t;
static res_t g_res_u64, g_res_fwd;

static void res_cb(const char *name, const ip_addr_t *ip, void *arg) {     /* lwIP context */
    res_t *r = (res_t *)arg;
    if (strcmp(name, r->host)) return;                   /* answer for a name that is no longer wanted */
    r->busy = false;
    if (ip) { r->ip = *ip; r->have = true; r->failed = false; r->next = make_timeout_time_ms(60000); }
    else { r->failed = true; r->next = make_timeout_time_ms(5000); }   /* not found: try again soon, keep an older answer */
}
static void res_set(res_t *r, const char *host) {
    if (!strcmp(r->host, host)) return;
    snprintf(r->host, sizeof r->host, "%s", host);
    r->have = false; r->busy = false; r->failed = false; r->next = get_absolute_time();
}
static void res_task(res_t *r) {
    if (!r->host[0] || r->busy || !time_reached(r->next)) return;
    ip_addr_t a;
    if (ipaddr_aton(r->host, &a)) { r->ip = a; r->have = true; r->failed = false; r->next = make_timeout_time_ms(3600000); return; }
    r->busy = true;
    r->next = make_timeout_time_ms(10000);               /* in case no answer ever comes */
    cyw43_arch_lwip_begin();
    err_t e = dns_gethostbyname(r->host, &a, res_cb, r);
    cyw43_arch_lwip_end();
    if (e == ERR_OK) { r->ip = a; r->have = true; r->busy = false; r->failed = false; r->next = make_timeout_time_ms(60000); }
    else if (e != ERR_INPROGRESS) { r->busy = false; r->failed = true; r->next = make_timeout_time_ms(5000); }
}
static uint32_t res_u32(const res_t *r) { return r->have ? ip4_addr_get_u32(ip_2_ip4(&r->ip)) : 0; }
static void u32_ntoa(uint32_t v, char *o, int n) {
    snprintf(o, (size_t)n, "%lu.%lu.%lu.%lu", (unsigned long)(v & 255), (unsigned long)((v >> 8) & 255),
             (unsigned long)((v >> 16) & 255), (unsigned long)(v >> 24));
}

/* ------------------------------------------------------------ audio stream (UDP): play + forward */
static struct udp_pcb *g_udp;
static srclock_t g_src;
static volatile uint32_t g_pkts = 0, g_last_pkt_us = 0, g_last_direct_us = 0, g_fwd_pkts = 0, g_fwd_err = 0, g_bad = 0;
static volatile uint32_t g_u64_ip = 0, g_fwd_ip = 0, g_my_ip = 0;   /* copies for the lwIP context */
static volatile uint32_t g_direct_ip = 0;                /* the Ultimate whose packets came last */
static volatile bool g_play = false;                     /* playing wanted (hand packets to the player) */
static uint32_t g_pps = 0, g_pkts_prev = 0, g_fpps = 0, g_fwd_prev = 0;
static void np_relayed(const uint8_t *query, int n);       /* a title sent along the stream chain (C64NP1) */
static uint32_t g_np_udp = 0;                            /* titles sent that way (no visualizer password) */

static void audio_recv(void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *addr, u16_t port) {
    (void)arg; (void)pcb; (void)port;
    static uint8_t buf[U64_PKT_BYTES];
    uint16_t len = p->tot_len;
    uint16_t n = pbuf_copy_partial(p, buf, sizeof buf, 0);
    pbuf_free(p);
    uint32_t from = ip4_addr_get_u32(ip_2_ip4(addr)), now = time_us_32();
    u64pkt_t pk;
    bool audio = len == n && u64pkt_parse(buf, n, &pk);
    /* only audio packets choose the source (see u64stream.h); others are just passed on */
    if (audio && !srclock_accept(&g_src, from, now)) return;   /* another stream is live: never mix two */
    if (audio) { g_pkts++; g_last_pkt_us = now; }
    if (from == g_u64_ip) { g_last_direct_us = now; g_direct_ip = from; }
    /* forward unchanged (also the Ultimate's 2-byte start packet), but never back to where it came from */
    if (len <= sizeof buf && fwd_wanted(g_cfg.fwd_on, g_fwd_ip, from, g_my_ip)) {
        struct pbuf *q = pbuf_alloc(PBUF_TRANSPORT, n, PBUF_RAM);
        if (q) {
            memcpy(q->payload, buf, n);
            ip_addr_t to; ip4_addr_set_u32(ip_2_ip4(&to), g_fwd_ip);
            if (udp_sendto(g_udp, q, &to, AUDIO_PORT) == ERR_OK) g_fwd_pkts++; else g_fwd_err++;
            pbuf_free(q);
        } else g_fwd_err++;
    }
    if (audio) { if (g_play) player_packet(pk.seq, pk.pcm); }
    else if (len == n && n > 7 && !memcmp(buf, "C64NP1 ", 7)) np_relayed(buf + 7, n - 7);   /* passed on above as well */
    else if (len != 2) g_bad++;
}
static bool rx_active(void) { return g_pkts && (uint32_t)(time_us_32() - g_last_pkt_us) < RX_TIMEOUT_US; }
static bool direct_active(void) { return g_last_direct_us && (uint32_t)(time_us_32() - g_last_direct_us) < RX_TIMEOUT_US; }

/* ------------------------------------------------------------ REST client: start / stop the Ultimate's stream */
enum { RQ_NONE, RQ_START, RQ_STOP };
typedef struct { bool on; ip_addr_t ip; char host[64], pw[33]; } rq_t;
static rq_t g_want[3];                                    /* pending, by kind: a stop goes out before a start */
static struct tcp_pcb *g_rest = NULL;
static volatile int g_rq = RQ_NONE;
static absolute_time_t g_rest_t0;
static char g_rest_req[400];
static char g_rbuf[600];
static int  g_rlen = 0;
static char g_u64state[64] = "nicht eingerichtet";
static volatile int g_rest_fail = 0;                     /* requests in a row without any answer (network dead?) */

static void rest_finish(void) {                          /* complete response received (lwIP context) */
    g_rbuf[g_rlen] = 0;
    int code = (strncmp(g_rbuf, "HTTP/1.", 7) == 0) ? atoi(g_rbuf + 9) : 0;
    DIAG("rest: Antwort %d (%s)", code, g_rq == RQ_START ? "start" : "stop");
    g_rest_fail = 0;
    if (code == 200)      strcpy(g_u64state, g_rq == RQ_START ? "Stream angefordert" : "Stream beendet");
    else if (code == 403) strcpy(g_u64state, "Passwort falsch (403)");
    else                  snprintf(g_u64state, sizeof g_u64state, "Fehler %d", code);
    g_rq = RQ_NONE;
}
static bool rest_close(struct tcp_pcb *pcb) {             /* true = the pcb had to be aborted (callers return ERR_ABRT) */
    tcp_arg(pcb, NULL); tcp_recv(pcb, NULL); tcp_err(pcb, NULL); tcp_sent(pcb, NULL); tcp_poll(pcb, NULL, 0);
    g_rest = NULL;
    if (tcp_close(pcb) == ERR_OK) return false;
    tcp_abort(pcb);
    return true;
}
static err_t rest_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err) {
    (void)arg; (void)err;
    if (!p) { rest_finish(); return rest_close(pcb) ? ERR_ABRT : ERR_OK; }   /* the Ultimate closed: response complete */
    int room = (int)sizeof g_rbuf - 1 - g_rlen;
    if (room > 0) g_rlen += pbuf_copy_partial(p, g_rbuf + g_rlen, (u16_t)(p->tot_len < room ? p->tot_len : room), 0);
    tcp_recved(pcb, p->tot_len);
    pbuf_free(p);
    return ERR_OK;
}
static void rest_err(void *arg, err_t err) {
    (void)arg;
    DIAG("rest: Verbindungsfehler err=%d", (int)err);
    g_rest = NULL;                                       /* pcb is already freed by lwIP */
    g_rq = RQ_NONE;
    g_rest_fail++;
    strcpy(g_u64state, "nicht erreichbar");
}
static err_t rest_poll(void *arg, struct tcp_pcb *pcb) {   /* every second: give up after 10 s */
    (void)arg;
    if (absolute_time_diff_us(g_rest_t0, get_absolute_time()) < 10000000) return ERR_OK;
    DIAG("rest: keine Antwort nach 10 s - abgebrochen");
    tcp_arg(pcb, NULL); tcp_recv(pcb, NULL); tcp_err(pcb, NULL); tcp_sent(pcb, NULL); tcp_poll(pcb, NULL, 0);
    tcp_abort(pcb);
    g_rest = NULL; g_rq = RQ_NONE;
    g_rest_fail++;
    strcpy(g_u64state, "keine Antwort");
    return ERR_ABRT;
}
static err_t rest_connected(void *arg, struct tcp_pcb *pcb, err_t err) {
    (void)arg;
    if (err != ERR_OK) { bool ab = rest_close(pcb); g_rq = RQ_NONE; strcpy(g_u64state, "nicht erreichbar"); return ab ? ERR_ABRT : ERR_OK; }
    tcp_write(pcb, g_rest_req, (u16_t)strlen(g_rest_req), TCP_WRITE_FLAG_COPY);
    tcp_output(pcb);
    return ERR_OK;
}
/* main loop: send one pending request (stop first) */
static void rest_task(void) {
    if (g_rq != RQ_NONE) return;
    int kind = g_want[RQ_STOP].on ? RQ_STOP : (g_want[RQ_START].on ? RQ_START : RQ_NONE);
    if (kind == RQ_NONE) return;
    rq_t *w = &g_want[kind];
    w->on = false;
    char pw[64] = "";
    if (w->pw[0]) snprintf(pw, sizeof pw, "X-Password: %s\r\n", w->pw);
    if (kind == RQ_START)
        snprintf(g_rest_req, sizeof g_rest_req,
                 "PUT /v1/streams/audio:start?ip=%s:%d HTTP/1.1\r\nHost: %s\r\n%sContent-Length: 0\r\nConnection: close\r\n\r\n",
                 g_ip, AUDIO_PORT, w->host, pw);
    else
        snprintf(g_rest_req, sizeof g_rest_req,
                 "PUT /v1/streams/audio:stop HTTP/1.1\r\nHost: %s\r\n%sContent-Length: 0\r\nConnection: close\r\n\r\n", w->host, pw);
    { char *e = strchr(g_rest_req, '\r'); DIAG("rest: %.*s an %s", e ? (int)(e - g_rest_req) : 40, g_rest_req, w->host); }
    cyw43_arch_lwip_begin();
    g_rlen = 0;
    g_rq = kind;
    g_rest = tcp_new_ip_type(IPADDR_TYPE_V4);
    if (g_rest) {
        tcp_recv(g_rest, rest_recv);
        tcp_err(g_rest, rest_err);
        tcp_poll(g_rest, rest_poll, 2);                   /* 2 x 0.5 s */
        g_rest_t0 = get_absolute_time();
        if (tcp_connect(g_rest, &w->ip, 80, rest_connected) != ERR_OK) { tcp_abort(g_rest); g_rest = NULL; g_rq = RQ_NONE; }
        else if (kind == RQ_START) strcpy(g_u64state, "verbinde ...");
    } else g_rq = RQ_NONE;
    cyw43_arch_lwip_end();
}
static void rest_want(int kind, ip_addr_t ip, const char *host, const char *pw) {
    rq_t *w = &g_want[kind];
    w->ip = ip; snprintf(w->host, sizeof w->host, "%s", host); snprintf(w->pw, sizeof w->pw, "%s", pw); w->on = true;
    if (kind == RQ_STOP) g_want[RQ_START].on = false;    /* a stop cancels an older start */
}

/* ------------------------------------------------------------ playback control (web page, button) */
static const u64cfg_t *cur_u64(void) { return &g_cfg.u64[g_prefs.src < U64_MAX ? g_prefs.src : 0]; }
static uint32_t g_taken = 0, g_taken_at[2];              /* the stream was taken by another device (last two times) */
static bool g_contested = false;
static absolute_time_t g_quiet_until;                    /* we ended the stream ourselves: not a takeover */
static char g_np[96] = "";                               /* now playing, reported by the PC jukebox */
static absolute_time_t g_np_until;

static bool g_force_req = false;                         /* new source: ask at once, the packets still coming are the old one's */
static void prefs_changed(void) { g_prefs_dirty = true; g_t_prefs = make_timeout_time_ms(2000); }
/* stop the stream at the Ultimate - only if it really streams to us (it may stream to the PC, which forwards) */
static void stop_direct(const u64cfg_t *u) {
    g_quiet_until = make_timeout_time_ms(3000);
    if (!direct_active() || !g_u64_ip || g_direct_ip != g_u64_ip) return;
    ip_addr_t ip; ip4_addr_set_u32(ip_2_ip4(&ip), g_u64_ip);
    rest_want(RQ_STOP, ip, u->addr, u->pw);
}
static void set_play(bool on) {
    if (on && ota_active()) return;                      /* no playback during a firmware update */
    if (on == g_play) return;
    DIAG("player: %s", on ? "Wiedergabe an" : "Wiedergabe aus");
    g_play = on;
    player_play(on);
    if (on) { g_t_req = get_absolute_time(); g_contested = false; }
    else stop_direct(cur_u64());
    g_prefs.play = on; prefs_changed();
}
static void set_source(int i) {
    if (i < 0 || i >= U64_MAX || i == g_prefs.src || !g_cfg.u64[i].addr[0]) return;
    DIAG("player: Quelle %d (%s)", i + 1, g_cfg.u64[i].addr);
    stop_direct(cur_u64());                              /* the old one stops streaming to us */
    g_last_direct_us = 0;
    g_prefs.src = (uint8_t)i; prefs_changed();
    res_set(&g_res_u64, g_cfg.u64[i].addr);
    g_u64_ip = 0;
    if (g_play) { player_restart(); g_t_req = get_absolute_time(); g_force_req = true; }
    g_contested = false;
}
/* the button runs on the main loop: take the lwIP lock, the web routes (lwIP context) change the same state */
void net_toggle_play(void) { cyw43_arch_lwip_begin(); set_play(!g_play); cyw43_arch_lwip_end(); }
void net_next_source(void) {
    cyw43_arch_lwip_begin();
    for (int k = 1; k <= U64_MAX; k++) {
        int i = (g_prefs.src + k) % U64_MAX;
        if (g_cfg.u64[i].addr[0]) { set_source(i); break; }
    }
    cyw43_arch_lwip_end();
}

/* ------------------------------------------------------------ web server */
#define REQ_MAX 2560
typedef struct {
    char  req[REQ_MAX];
    int   req_len, content_len, body_len;
    bool  header_done, is_post, body_to_g, done;
    char  path[400];
    char  auth[900];                                           /* value of the Authorization header */
    char  method[8];
    char  hdr[320];
    char  dyn[512];                                            /* small dynamic bodies (replies) */
    const char *tx_body;
    int   tx_hdr_len, tx_body_len, tx_pos;
    char *own;                                                 /* body allocated for this answer (freed with the connection) */
    int   poll_pos;
} conn_t;

static int g_conns = 0;
static void *g_body_owner = NULL;                        /* the connection whose POST body is going into g_body */

static bool conn_close(struct tcp_pcb *pcb, conn_t *c) {  /* true = aborted: the lwIP callback must return ERR_ABRT */
    tcp_arg(pcb, NULL); tcp_recv(pcb, NULL); tcp_sent(pcb, NULL); tcp_err(pcb, NULL); tcp_poll(pcb, NULL, 0);
    if (c) { if (g_body_owner == c) g_body_owner = NULL; free(c->own); free(c); g_conns--; }
    if (tcp_close(pcb) == ERR_OK) return false;
    tcp_abort(pcb);
    return true;
}
static void send_more(struct tcp_pcb *pcb, conn_t *c) {
    int total = c->tx_hdr_len + c->tx_body_len;
    while (c->tx_pos < total) {
        int room = tcp_sndbuf(pcb);
        if (room <= 0) break;
        const char *src; int left;
        if (c->tx_pos < c->tx_hdr_len) { src = c->hdr + c->tx_pos; left = c->tx_hdr_len - c->tx_pos; }
        else { src = c->tx_body + (c->tx_pos - c->tx_hdr_len); left = total - c->tx_pos; }
        int n = left < room ? left : room;
        if (n > 1460) n = 1460;
        if (tcp_write(pcb, src, (u16_t)n, TCP_WRITE_FLAG_COPY | (c->tx_pos + n < total ? TCP_WRITE_FLAG_MORE : 0)) != ERR_OK) break;
        c->tx_pos += n;
    }
    tcp_output(pcb);
}
static void respond(struct tcp_pcb *pcb, conn_t *c, int code, const char *type, const char *body, int len) {
    c->tx_hdr_len = snprintf(c->hdr, sizeof c->hdr,
        "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %d\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n",
        code, code == 200 ? "OK" : (code == 404 ? "Not Found" : "Error"), type, len);
    c->tx_body = body; c->tx_body_len = len; c->tx_pos = 0; c->done = true;
    send_more(pcb, c);
}
static void reply_text(struct tcp_pcb *pcb, conn_t *c, int code, const char *t) {
    snprintf(c->dyn, sizeof c->dyn, "%s", t);
    respond(pcb, c, code, "text/plain; charset=utf-8", c->dyn, (int)strlen(c->dyn));
}
static void reply_json(struct tcp_pcb *pcb, conn_t *c, int code, const char *t) {
    snprintf(c->dyn, sizeof c->dyn, "%s", t);
    respond(pcb, c, code, "application/json", c->dyn, (int)strlen(c->dyn));
}

/* text for a JSON string: " and \ escaped, control characters dropped */
static void jesc(char *o, int n, const char *s) {
    int k = 0;
    for (; *s && k < n - 2; s++) {
        unsigned char ch = (unsigned char)*s;
        if (ch < 32) continue;
        if (ch == '"' || ch == '\\') { if (k >= n - 3) break; o[k++] = '\\'; }
        o[k++] = (char)ch;
    }
    o[k] = 0;
}
/* x-www-form-urlencoded: copy the value of key into out; false if the key is missing */
static bool form_value(const char *body, const char *key, char *out, int outlen) {
    out[0] = 0;
    size_t kl = strlen(key);
    for (const char *p = body; p && *p; ) {
        if (!strncmp(p, key, kl) && p[kl] == '=') {
            p += kl + 1;
            int n = 0;
            while (*p && *p != '&' && n < outlen - 1) {
                if (*p == '+') { out[n++] = ' '; p++; }
                else if (*p == '%' && p[1] && p[2]) { char h[3] = { p[1], p[2], 0 }; out[n++] = (char)strtol(h, NULL, 16); p += 3; }
                else out[n++] = *p++;
            }
            out[n] = 0;
            return true;
        }
        p = strchr(p, '&');
        if (p) p++;
    }
    return false;
}
static const char *form_ptr(const char *body, const char *key, int *len) {   /* raw value (no decoding), NULL = missing */
    size_t kl = strlen(key);
    for (const char *p = body; p && *p; p = strchr(p, '&'), p = p ? p + 1 : NULL)
        if (!strncmp(p, key, kl) && p[kl] == '=') { p += kl + 1; const char *e = strchr(p, '&'); *len = e ? (int)(e - p) : (int)strlen(p); return p; }
    return NULL;
}
/* backup: binary blocks as hex text */
static int hex_out(char *o, const void *d, int n) {
    static const char hx[] = "0123456789abcdef";
    const uint8_t *b = (const uint8_t *)d;
    for (int i = 0; i < n; i++) { o[2 * i] = hx[b[i] >> 4]; o[2 * i + 1] = hx[b[i] & 15]; }
    return 2 * n;
}
static int hex_val(char c) { return c >= '0' && c <= '9' ? c - '0' : (c >= 'a' && c <= 'f' ? c - 'a' + 10 : (c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1)); }
static int hex_in(uint8_t *o, int max, const char *h, int len) {   /* bytes, -1 = not hex / too long */
    if (len & 1 || len / 2 > max) return -1;
    for (int i = 0; i < len / 2; i++) {
        int a = hex_val(h[2 * i]), b = hex_val(h[2 * i + 1]);
        if (a < 0 || b < 0) return -1;
        o[i] = (uint8_t)(a << 4 | b);
    }
    return len / 2;
}
static bool qparam(const char *path, const char *key, char *out, int outlen) {
    const char *q = strchr(path, '?');
    if (!q) { out[0] = 0; return false; }
    return form_value(q + 1, key, out, outlen);
}

/* ---- HTTP Digest authentication (RFC 7616, MD5, qop=auth) - as in the visualizer ----
   The password never travels over the network. Nonces live 1 hour; every counter value may be used once. */
#define NONCES     6
#define NONCE_LIFE_US (3600ull * 1000000ull)
typedef struct { char n[33]; uint64_t born; uint32_t max_nc, seen; bool used; } nonce_t;
static nonce_t g_nonce[NONCES];
static int g_nonce_next = 0;

static const char *new_nonce(void) {
    nonce_t *e = &g_nonce[g_nonce_next];
    g_nonce_next = (g_nonce_next + 1) % NONCES;
    snprintf(e->n, sizeof e->n, "%08lx%08lx%08lx%08lx", (unsigned long)get_rand_32(), (unsigned long)get_rand_32(),
             (unsigned long)get_rand_32(), (unsigned long)time_us_32());
    e->born = time_us_64(); e->max_nc = 0; e->seen = 0; e->used = true;
    return e->n;
}
static bool digest_param(const char *h, const char *key, char *out, int outlen) {
    size_t kl = strlen(key);
    for (const char *p = h; (p = strstr(p, key)) != NULL; p += kl) {
        if (p != h && p[-1] != ' ' && p[-1] != ',') continue;  /* "nonce" must not match "cnonce" */
        const char *v = p + kl;
        while (*v == ' ') v++;
        if (*v != '=') continue;
        v++;
        while (*v == ' ') v++;
        bool q = *v == '"';
        if (q) v++;
        int n = 0;
        while (*v && n < outlen - 1 && (q ? *v != '"' : (*v != ',' && *v != ' ' && *v != '\r'))) out[n++] = *v++;
        out[n] = 0;
        return true;
    }
    return false;
}
/* 1 = ok, 0 = wrong / missing, -1 = right password but unknown or expired nonce (stale) */
static int auth_check(const conn_t *c) {
    if (strncmp(c->auth, "Digest ", 7)) return 0;
    const char *h = c->auth + 7;
    char user[64], realm[40], nonce[40], uri[400], resp[40], qop[16], nc[12], cnonce[64];
    if (!digest_param(h, "username", user, sizeof user) || !digest_param(h, "realm", realm, sizeof realm) ||
        !digest_param(h, "nonce", nonce, sizeof nonce) || !digest_param(h, "uri", uri, sizeof uri) ||
        !digest_param(h, "response", resp, sizeof resp) || !digest_param(h, "qop", qop, sizeof qop) ||
        !digest_param(h, "nc", nc, sizeof nc) || !digest_param(h, "cnonce", cnonce, sizeof cnonce)) return 0;
    if (strcmp(realm, REALM) || strcmp(qop, "auth") || strcmp(uri, c->path)) return 0;
    char buf[520], ha1[33], ha2[33], expect[33];
    snprintf(buf, sizeof buf, "%s:%s:%s", user, REALM, web_password());
    md5_hex(buf, strlen(buf), ha1);
    snprintf(buf, sizeof buf, "%s:%s", c->method, uri);
    md5_hex(buf, strlen(buf), ha2);
    snprintf(buf, sizeof buf, "%s:%s:%s:%s:%s:%s", ha1, nonce, nc, cnonce, qop, ha2);
    md5_hex(buf, strlen(buf), expect);
    if (strcmp(expect, resp)) return 0;                   /* wrong password */
    for (int i = 0; i < NONCES; i++) {
        nonce_t *e = &g_nonce[i];
        if (!e->used || strcmp(e->n, nonce)) continue;
        if (time_us_64() - e->born > NONCE_LIFE_US) return -1;
        uint32_t v = (uint32_t)strtoul(nc, NULL, 16);
        if (v > e->max_nc) {                              /* new highest counter: slide the window */
            uint32_t shift = v - e->max_nc;
            e->seen = shift >= 32 ? 0 : e->seen << shift;
            e->seen |= 1u;
            e->max_nc = v;
            return 1;
        }
        uint32_t back = e->max_nc - v;                    /* parallel requests may arrive out of order */
        if (back < 32 && !(e->seen & (1u << back))) { e->seen |= 1u << back; return 1; }
        return 0;                                         /* counter used before: replay */
    }
    return -1;                                            /* nonce unknown (e.g. after a restart) */
}
static void respond_401(struct tcp_pcb *pcb, conn_t *c, bool stale) {
    static const char body[] = "Passwort erforderlich (Benutzername beliebig).";
    c->tx_hdr_len = snprintf(c->hdr, sizeof c->hdr,
        "HTTP/1.1 401 Unauthorized\r\nWWW-Authenticate: Digest realm=\"" REALM "\", qop=\"auth\", algorithm=MD5, "
        "nonce=\"%s\"%s\r\nContent-Type: text/plain; charset=utf-8\r\nContent-Length: %d\r\nConnection: close\r\n\r\n",
        new_nonce(), stale ? ", stale=true" : "", (int)sizeof body - 1);
    c->tx_body = body; c->tx_body_len = (int)sizeof body - 1; c->tx_pos = 0; c->done = true;
    send_more(pcb, c);
}

/* ---- /status ---- */
static const char *state_name(int st) { return st == JIT_PLAY ? "spielt" : (st == JIT_PREFILL ? "puffert" : "aus"); }
static int status_json(char *o, int cap) {
    player_stats_t s; player_stats(&s);
    char ap_text[56]; snprintf(ap_text, sizeof ap_text, "eigener Zugangspunkt %s", g_ap_ssid);
    char e_wifi[120], e_ssid[70], e_state[100], e_np[200], e_fwd[130], from[16] = "-", u64ip[16] = "", fwdip[16] = "-";
    jesc(e_wifi, sizeof e_wifi, g_state == NS_STA ? g_cfg.ssid : (g_state == NS_AP ? ap_text : "verbinde ..."));
    jesc(e_ssid, sizeof e_ssid, g_cfg.ssid);
    jesc(e_state, sizeof e_state, g_u64state);
    jesc(e_np, sizeof e_np, time_reached(g_np_until) ? "" : g_np);
    jesc(e_fwd, sizeof e_fwd, g_cfg.fwd);
    if (rx_active()) u32_ntoa(g_src.ip, from, sizeof from);
    if (g_u64_ip) u32_ntoa(g_u64_ip, u64ip, sizeof u64ip);
    if (g_fwd_ip) u32_ntoa(g_fwd_ip, fwdip, sizeof fwdip);
    const u64cfg_t *u = cur_u64();
    char e_cur[130]; jesc(e_cur, sizeof e_cur, u64ip[0] ? u64ip : u->addr);
    int n = snprintf(o, (size_t)cap,
        "{\"fw\":\"" FW_MARKER "\",\"build\":\"%s\",\"wifi\":\"%s\",\"ssid\":\"%s\",\"ip\":\"%s\",\"sta\":%s,\"name\":\"%s\",\"cfgname\":\"%s\",\"pwdefault\":%s,"
        "\"u64\":\"%s\",\"src\":%d,\"u64state\":\"%s\",\"play\":%s,\"state\":\"%s\",\"rx\":%s,\"from\":\"%s\",\"direct\":%s,\"pps\":%lu,"
        "\"vol\":%d,\"fill\":%.1f,\"avg\":%.1f,\"target\":%d,\"ppm\":%.0f,\"rate\":%.1f,"
        "\"missing\":%lu,\"late\":%lu,\"dup\":%lu,\"underruns\":%lu,\"concealed\":%lu,\"silent\":%lu,\"skipped\":%lu,\"restarts\":%lu,"
        "\"prefills\":%lu,\"qdrop\":%lu,\"behind\":%lu,\"rejected\":%lu,\"bad\":%lu,"
        "\"fed\":%s,\"fwd_on\":%s,\"fwd\":\"%s\",\"fwdip\":\"%s\",\"fpps\":%lu,\"fwderr\":%lu,\"taken\":%lu,\"contested\":%s,\"np\":\"%s\",\"lat\":%d,\"fwdpw\":%s,\"npr\":\"%s\",\"nprsent\":%lu,\"scanning\":%s,\"u64list\":[",
        ota_build(), e_wifi, e_ssid, g_ip, g_state == NS_STA ? "true" : "false", g_state == NS_STA ? mdnsl_name() : base_name(), g_cfg.name,
        g_cfg.webpw[0] ? "false" : "true",
        e_cur, g_prefs.src, e_state, g_play ? "true" : "false", state_name(s.state), rx_active() ? "true" : "false", from,
        direct_active() ? "true" : "false", (unsigned long)g_pps,
        s.volume, (double)s.fill_ms, (double)s.avg_ms, s.target_ms, (double)s.ppm, s.rate,
        (unsigned long)s.jit.missing, (unsigned long)s.jit.late, (unsigned long)s.jit.dup, (unsigned long)s.jit.underruns,
        (unsigned long)s.jit.concealed, (unsigned long)s.jit.silent, (unsigned long)s.jit.skipped, (unsigned long)s.jit.restarts,
        (unsigned long)s.jit.prefills, (unsigned long)s.qdrop, (unsigned long)s.behind, (unsigned long)g_src.rejected, (unsigned long)g_bad,
        g_cfg.fed ? "true" : "false", g_cfg.fwd_on ? "true" : "false", e_fwd, fwdip, (unsigned long)g_fpps, (unsigned long)g_fwd_err, (unsigned long)g_taken,
        g_contested ? "true" : "false", e_np, g_cfg.latency_ms, g_cfg.fwdpw[0] ? "true" : "false",
        !g_cfg.fwdpw[0] && g_np_udp ? "gesendet (ohne Passwort)" : npr_state(), (unsigned long)(!g_cfg.fwdpw[0] && g_np_udp ? g_np_udp : npr_sent()), u64scan_running() ? "true" : "false");
    for (int i = 0; i < U64_MAX && n < cap; i++) {
        char l[40], a[130]; jesc(l, sizeof l, g_cfg.u64[i].label); jesc(a, sizeof a, g_cfg.u64[i].addr);
        n += snprintf(o + n, (size_t)(cap - n), "%s{\"label\":\"%s\",\"addr\":\"%s\",\"pw\":%s}", i ? "," : "", l, a,
                      g_cfg.u64[i].pw[0] ? "true" : "false");
    }
    if (n < cap) n += snprintf(o + n, (size_t)(cap - n), "]}");
    return n >= cap ? cap - 1 : n;
}

/* ---- the beacon of this receiver (devices.c): who it is and how the stream flows through it */
#ifdef DEBUG_USB
#define IMG_NAME "c64audio_debug"                        /* the firmware file it needs (tools/update_all.py) */
#else
#define IMG_NAME "c64audio"
#endif
int net_self_json(char *o, int n) {                      /* lwIP lock held (main loop or web route) */
    static char e_name[40], e_fwd[130], e_src[130], e_lbl[40], from[16], fwip[16], u64ip[16];
    const u64cfg_t *u = cur_u64();
    from[0] = fwip[0] = u64ip[0] = 0;
    if (rx_active()) u32_ntoa(g_src.ip, from, sizeof from);
    if (g_fwd_ip) u32_ntoa(g_fwd_ip, fwip, sizeof fwip);
    if (g_u64_ip) u32_ntoa(g_u64_ip, u64ip, sizeof u64ip);
    jesc(e_name, sizeof e_name, mdnsl_name()); jesc(e_fwd, sizeof e_fwd, g_cfg.fwd_on ? g_cfg.fwd : "");
    jesc(e_src, sizeof e_src, u64ip[0] ? u64ip : u->addr); jesc(e_lbl, sizeof e_lbl, u->label);
    return snprintf(o, (size_t)n,
        "{\"t\":\"audio\",\"n\":\"%s\",\"ip\":\"%s\",\"b\":\"%s\",\"img\":\"" IMG_NAME "\",\"up\":%lu,"
        "\"st\":%d,\"sm\":%d,\"rx\":%lu,\"from\":\"%s\",\"fw\":\"%s\",\"fwip\":\"%s\",\"fp\":%lu,\"src\":\"%s\",\"srcl\":\"%s\"}",
        e_name, g_ip, ota_build(), (unsigned long)(to_ms_since_boot(get_absolute_time()) / 1000),
        g_play ? 1 : 0, g_cfg.fed ? 2 : 0, (unsigned long)(rx_active() ? g_pps : 0), from, e_fwd, fwip, (unsigned long)(g_cfg.fwd_on ? g_fpps : 0), e_src, e_lbl);
}

/* ---- /config: the settings form; empty password fields keep the stored one ---- */
static void handle_config(struct tcp_pcb *pcb, conn_t *c) {
    g_body[c->body_len] = 0;
    netcfg_t n = g_cfg;
    char v[72], k[8];
    if (form_value(g_body, "ssid", v, sizeof v)) snprintf(n.ssid, sizeof n.ssid, "%s", v);
    if (form_value(g_body, "pass", v, sizeof v) && v[0]) snprintf(n.pass, sizeof n.pass, "%s", v);
    if (form_value(g_body, "name", v, sizeof v)) {           /* device name: lower case; empty = c64audio */
        for (char *p = v; *p; p++) if (*p >= 'A' && *p <= 'Z') *p += 32;
        if (v[0] && !mdnsl_name_valid(v)) { reply_text(pcb, c, 400, "Gerätename: 1 bis 16 Zeichen, nur a-z, 0-9 und Bindestrich (nicht am Anfang oder Ende)."); return; }
        if (!strcmp(v, DEVICE_NAME_DEFAULT)) v[0] = 0;
        memset(n.name, 0, sizeof n.name); snprintf(n.name, sizeof n.name, "%s", v);
    }
    for (int i = 0; i < U64_MAX; i++) {
        snprintf(k, sizeof k, "l%d", i); if (form_value(g_body, k, v, sizeof v)) snprintf(n.u64[i].label, sizeof n.u64[i].label, "%s", v);
        snprintf(k, sizeof k, "a%d", i);
        if (form_value(g_body, k, v, sizeof v)) {
            char *s = v; while (*s == ' ') s++;              /* "http://ultimate/" as copied from the browser -> "ultimate" */
            if (!strncmp(s, "http://", 7)) s += 7;
            char *e = strchr(s, '/'); if (e) *e = 0;
            for (e = s + strlen(s); e > s && e[-1] == ' '; ) *--e = 0;
            snprintf(n.u64[i].addr, sizeof n.u64[i].addr, "%s", s);
            if (!s[0]) n.u64[i].pw[0] = 0;
        }
        snprintf(k, sizeof k, "p%d", i); if (form_value(g_body, k, v, sizeof v) && v[0]) snprintf(n.u64[i].pw, sizeof n.u64[i].pw, "%s", v);
        snprintf(k, sizeof k, "x%d", i); if (form_value(g_body, k, v, sizeof v) && v[0] == '1') n.u64[i].pw[0] = 0;
    }
    if (form_value(g_body, "fwd", v, sizeof v)) {
        char *s = v; while (*s == ' ') s++;
        if (!strncmp(s, "http://", 7)) s += 7;
        char *e = strchr(s, '/'); if (e) *e = 0;
        snprintf(n.fwd, sizeof n.fwd, "%s", s);
    }
    if (form_value(g_body, "fwd_on", v, sizeof v)) n.fwd_on = v[0] == '1';
    if (form_value(g_body, "fed", v, sizeof v)) n.fed = v[0] == '1';
    if (form_value(g_body, "fwdpw", v, sizeof v) && v[0]) snprintf(n.fwdpw, sizeof n.fwdpw, "%s", v);
    if (form_value(g_body, "xfwdpw", v, sizeof v) && v[0] == '1') n.fwdpw[0] = 0;
    if (form_value(g_body, "lat", v, sizeof v)) {
        int l = atoi(v);
        if (l < LATENCY_MIN_MS || l > LATENCY_MAX_MS) { reply_text(pcb, c, 400, "Puffer: 40 bis 300 ms."); return; }
        n.latency_ms = (uint16_t)l;
    }
    if (form_value(g_body, "webpw", v, sizeof v) && v[0]) {
        if (strlen(v) < 6 || strlen(v) > 32) { reply_text(pcb, c, 400, "Das Passwort für die Webseite muss 6 bis 32 Zeichen lang sein."); return; }
        snprintf(n.webpw, sizeof n.webpw, "%s", v);
    }
    /* the setup assistant of the visualizer also sets the source (it may be a new row) and switches playing on */
    int nsrc = -1, nplay = -1;
    if (form_value(g_body, "src", v, sizeof v) && v[0]) {
        nsrc = atoi(v);
        if (!(v[0] >= '0' && v[0] <= '9') || nsrc >= U64_MAX || !n.u64[nsrc].addr[0]) { reply_text(pcb, c, 400, "Diese Quelle ist nicht eingerichtet."); return; }
    }
    if (form_value(g_body, "play", v, sizeof v) && v[0]) nplay = v[0] == '1';
    g_newcfg = n; g_newsrc = nsrc; g_newplay = nplay;
    g_pending_cfg = true;
    reply_text(pcb, c, 200, "Gespeichert - der Pico startet neu und verbindet sich.");
}

/* ---- /jb/np: "now playing" from the Ultimate64 SID Jukebox on the PC (it sends this to the visualizer; if
   this receiver is entered there instead, the title shows on the status page). Same answers as the visualizer. */
static bool np_apply(const char *path) {                 /* "/jb/np?..." -> status page; false = no title */
    char t[8], title[40], author[40];
    qparam(path, "stop", t, sizeof t);
    if (t[0] == '1') { g_np[0] = 0; return true; }
    qparam(path, "title", title, sizeof title);
    qparam(path, "author", author, sizeof author);
    if (!title[0] && !author[0]) return false;
    qparam(path, "new", t, sizeof t);
    if (t[0] == '1' || !g_np[0]) snprintf(g_np, sizeof g_np, "%s%s%s", title, title[0] && author[0] ? " \xE2\x80\x93 " : "", author);
    g_np_until = make_timeout_time_ms(75000);            /* the PC repeats every 30 s while it plays */
    return true;
}
/* without the visualizer's password: the title goes along the stream chain as "C64NP1 <query>" to the stream port of
   the forwarding target (the visualizer takes it there without a password, from firmware 2026-10-18 on) */
static void np_udp(const char *query) {                  /* lwIP context */
    int n = (int)strlen(query);
    if (!g_udp || !g_fwd_ip || g_fwd_ip == g_my_ip || n > U64_PKT_BYTES - 7) return;
    struct pbuf *q = pbuf_alloc(PBUF_TRANSPORT, (u16_t)(n + 7), PBUF_RAM);
    if (!q) return;
    memcpy(q->payload, "C64NP1 ", 7); memcpy((char *)q->payload + 7, query, (size_t)n);
    ip_addr_t to; ip4_addr_set_u32(ip_2_ip4(&to), g_fwd_ip);
    if (udp_sendto(g_udp, q, &to, AUDIO_PORT) == ERR_OK) g_np_udp++;
    pbuf_free(q);
}
static void np_relayed(const uint8_t *query, int n) {    /* lwIP context (audio_recv) */
    static char path[U64_PKT_BYTES + 8];
    memcpy(path, "/jb/np?", 7); memcpy(path + 7, query, (size_t)n); path[7 + n] = 0;
    np_apply(path);
}
static void handle_np(struct tcp_pcb *pcb, conn_t *c) {
    if (g_cfg.fwd_on && g_cfg.fwd[0]) {                  /* passed on unchanged to the visualizer */
        const char *q = strchr(c->path, '?');
        if (g_cfg.fwdpw[0]) npr_queue(c->path);          /* web page with Digest login (np_relay.c) */
        else if (q) np_udp(q + 1);                       /* along the stream chain */
    }
    if (np_apply(c->path)) reply_json(pcb, c, 200, "{\"ok\":true}");
    else reply_json(pcb, c, 400, "{\"ok\":false,\"msg\":\"title fehlt\"}");
}

static absolute_time_t g_t_web;                          /* last web request (the AP is not switched off while in use) */
static void handle(struct tcp_pcb *pcb, conn_t *c) {
    g_t_web = get_absolute_time();
    /* in the home network the page needs the password; on the Pico's own access point the WLAN password
       already protects it (and that is the way back if the password is forgotten) */
    if (g_state == NS_STA) {
        int a = auth_check(c);
        if (a != 1) { respond_401(pcb, c, a < 0); return; }
    }
    char v[16];
    if (!c->is_post && !strcmp(c->path, "/")) {
        respond(pcb, c, 200, "text/html; charset=utf-8", WEB_PAGE, (int)sizeof WEB_PAGE - 1);
    } else if (!c->is_post && !strcmp(c->path, "/hilfe")) {
        /* the manual (docs/HANDBUCH.md) for the "Hilfe" tab, stored gzip-compressed in flash */
        respond(pcb, c, 200, "text/html; charset=utf-8\r\nContent-Encoding: gzip", (const char *)HELP_GZ, HELP_GZ_LEN);
    } else if (!c->is_post && !strcmp(c->path, "/devices")) {
        /* the C64 devices in the network (beacons, devices.c) - a copy only for as long as it is being sent */
        int max = 12288;
        while (!(c->own = malloc((size_t)max)) && max > 2048) max /= 2;
        int n = c->own ? dev_json(c->own, max) : 0;
        respond(pcb, c, c->own ? 200 : 503, "application/json", c->own ? c->own : "", n);
    } else if (c->is_post && !strcmp(c->path, "/devices/query")) {
        dev_query();
        reply_text(pcb, c, 200, "OK");
    } else if (!c->is_post && !strcmp(c->path, "/status")) {
        int cap = 2304;
        char *buf = c->own = malloc((size_t)cap);              /* freed with the connection */
        if (!buf) { reply_text(pcb, c, 503, "Speicher knapp - bitte gleich nochmal."); return; }
        respond(pcb, c, 200, "application/json", buf, status_json(buf, cap));
    } else if (!c->is_post && !strcmp(c->path, "/diag")) {
        int max = 12288;
        while (!(c->own = malloc((size_t)max)) && max > 1024) max /= 2;
        int n = c->own ? diag_snapshot(c->own, max) : 0;
        respond(pcb, c, c->own ? 200 : 503, "text/plain; charset=utf-8", c->own ? c->own : "", n);
    } else if (c->is_post && !strncmp(c->path, "/cmd?", 5)) {
        char cmd[16];
        qparam(c->path, "c", cmd, sizeof cmd);
        if (!strcmp(cmd, "play")) set_play(true);
        else if (!strcmp(cmd, "stop")) set_play(false);
        else if (!strcmp(cmd, "vol")) {
            qparam(c->path, "v", v, sizeof v);
            int p = atoi(v);
            if (!(v[0] >= '0' && v[0] <= '9') || p > 100) { reply_text(pcb, c, 400, "Lautstärke: 0 bis 100 %."); return; }
            if (p != g_prefs.volume) { g_prefs.volume = (uint8_t)p; player_set_volume(p); prefs_changed(); }
        } else if (!strcmp(cmd, "src")) {
            qparam(c->path, "i", v, sizeof v);
            int i = atoi(v);
            if (!(v[0] >= '0' && v[0] <= '9') || i >= U64_MAX || !g_cfg.u64[i].addr[0]) { reply_text(pcb, c, 400, "Diese Quelle ist nicht eingerichtet."); return; }
            set_source(i);
        } else if (!strcmp(cmd, "reset")) {
            player_reset_stats(); g_taken = 0; g_taken_at[0] = g_taken_at[1] = 0; g_contested = false;
            g_src.rejected = 0; g_bad = 0; g_fwd_err = 0;
        }
        else { reply_text(pcb, c, 400, "Unbekannter Befehl."); return; }
        reply_text(pcb, c, 200, "OK");
    } else if (c->is_post && !strcmp(c->path, "/config")) {
        if (!c->body_to_g) { reply_text(pcb, c, 400, "Ungültige Daten."); return; }
        handle_config(pcb, c);
    } else if (!c->is_post && !strcmp(c->path, "/backup")) {
        /* all settings as a file: WLAN, web password, name, Ultimates, forwarding, buffer (netcfg) and the current
           volume / source / on-off (one prefs record). Own "fmt": a visualizer backup is refused here and vice versa. */
        static netcfg_t w; w = g_cfg; w.magic = NETCFG_MAGIC; w.check = cfg_check(&w);   /* static: off the 2 KB stack */
        uint8_t pr[PREFS_PACKED]; prefs_pack(&g_prefs, pr);
        int cap = 2 * ((int)sizeof w + PREFS_PACKED) + 200;
        char *b = c->own = malloc((size_t)cap);
        if (!b) { reply_text(pcb, c, 503, "Speicher knapp - bitte gleich nochmal."); return; }
        int k = snprintf(b, (size_t)cap, "{\"fmt\":\"c64audio-backup\",\"ver\":1,\"build\":\"%s\",\"name\":\"%s\",\"netcfg\":\"", ota_build(), base_name());
        k += hex_out(b + k, &w, (int)sizeof w);
        k += snprintf(b + k, (size_t)(cap - k), "\",\"prefs\":\"");
        k += hex_out(b + k, pr, PREFS_PACKED);
        k += snprintf(b + k, (size_t)(cap - k), "\"}");
        respond(pcb, c, 200, "application/json", b, k);
    } else if (c->is_post && !strcmp(c->path, "/restore")) {
        /* everything is checked here, the main loop only writes: a broken or foreign file changes nothing */
        if (!c->body_to_g) { reply_text(pcb, c, 400, "Ungültige Daten."); return; }
        g_body[c->body_len] = 0;
        int ln = 0, lp = 0;
        const char *hn = form_ptr(g_body, "netcfg", &ln), *hp = form_ptr(g_body, "prefs", &lp);
        static netcfg_t n;                                   /* static: off the 2 KB stack */
        if (!hn || hex_in((uint8_t *)&n, (int)sizeof n, hn, ln) != (int)sizeof n || n.magic != NETCFG_MAGIC || n.check != cfg_check(&n)) {
            reply_text(pcb, c, 400, "Die Sicherung passt nicht (WLAN-/Ultimate-Einstellungen fehlen oder sind beschädigt)."); return;
        }
        uint8_t pr[PREFS_PACKED]; prefs_t p;
        if (hp && (hex_in(pr, PREFS_PACKED, hp, lp) != PREFS_PACKED || !prefs_unpack(pr, PREFS_PACKED, &p))) {
            reply_text(pcb, c, 400, "Die Sicherung passt nicht (Lautstärke/Quelle beschädigt)."); return;
        }
        g_restcfg = n; g_rest_has_prefs = hp != NULL; if (hp) g_restprefs = p;
        g_pending_restore = true;
        reply_text(pcb, c, 200, "Einstellungen wiederhergestellt - der Pico startet gleich neu.");
    } else if (!strcmp(c->path, "/u64/scan")) {               /* search the home network for Ultimates */
        if (c->is_post) {
            if (g_state != NS_STA) { reply_text(pcb, c, 409, "Die Suche geht nur im Heim-WLAN."); return; }
            u64scan_start(ip4_addr_get_u32(netif_ip4_addr(&cyw43_state.netif[CYW43_ITF_STA])));   /* only sets variables - the main loop works */
        }
        char *b = c->own = malloc(1600);                    /* up to 8 devices (the small per-connection buffer holds ~3) */
        if (!b) { reply_text(pcb, c, 503, "Speicher knapp - bitte gleich nochmal."); return; }
        respond(pcb, c, 200, "application/json", b, u64scan_json(b, 1600));
    } else if (c->is_post && !strncmp(c->path, "/jb/np", 6) && (c->path[6] == '?' || !c->path[6])) {
        handle_np(pcb, c);
    } else {
        const char *o, *type; int olen;
        int code = ota_route(c->is_post, c->path, c->body_to_g ? g_body : NULL, c->body_len, &o, &olen, &type);
        if (code) {
            char *copy = olen <= (int)sizeof c->dyn ? c->dyn : (c->own = malloc((size_t)olen));
            if (!copy) { reply_text(pcb, c, 503, "Speicher knapp - bitte gleich nochmal."); return; }
            memcpy(copy, o, (size_t)olen);
            respond(pcb, c, code, type, copy, olen);
        }
        else reply_text(pcb, c, 404, "Nicht gefunden");
    }
}

static err_t http_sent(void *arg, struct tcp_pcb *pcb, u16_t len) {
    (void)len;
    conn_t *c = (conn_t *)arg;
    if (!c) return ERR_OK;
    send_more(pcb, c);
    if (c->done && c->tx_pos >= c->tx_hdr_len + c->tx_body_len && tcp_sndqueuelen(pcb) == 0) return conn_close(pcb, c) ? ERR_ABRT : ERR_OK;
    return ERR_OK;
}
static err_t http_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err) {
    (void)err;
    conn_t *c = (conn_t *)arg;
    if (!p) return conn_close(pcb, c) ? ERR_ABRT : ERR_OK;
    tcp_recved(pcb, p->tot_len);
    if (!c || c->done) { pbuf_free(p); return ERR_OK; }
    for (struct pbuf *q = p; q; q = q->next) {
        const char *d = (const char *)q->payload;
        for (int i = 0; i < q->len; i++) {
            if (!c->header_done) {
                if (c->req_len < REQ_MAX - 1) c->req[c->req_len++] = d[i];
                c->req[c->req_len] = 0;
                if (c->req_len >= 4 && !memcmp(c->req + c->req_len - 4, "\r\n\r\n", 4)) {   /* end of the header */
                    c->header_done = true;
                    c->is_post = !strncmp(c->req, "POST ", 5);
                    const char *u = strchr(c->req, ' ');
                    int n = 0;
                    if (u) { u++; while (*u && *u != ' ' && n < (int)sizeof c->path - 1) c->path[n++] = *u++; }
                    c->path[n] = 0;
                    { int k = 0; while (c->req[k] && c->req[k] != ' ' && k < 7) { c->method[k] = c->req[k]; k++; } c->method[k] = 0; }
                    const char *au = strstr(c->req, "Authorization: ");
                    if (au) {
                        au += 15;
                        int k = 0;
                        while (au[k] && au[k] != '\r' && k < (int)sizeof c->auth - 1) { c->auth[k] = au[k]; k++; }
                        c->auth[k] = 0;
                    }
                    const char *cl = strstr(c->req, "Content-Length:");
                    if (!cl) cl = strstr(c->req, "content-length:");
                    c->content_len = cl ? atoi(cl + 15) : 0;
                    c->body_to_g = c->is_post && c->content_len > 0 && c->content_len < (int)sizeof g_body - 32
                                   && !g_pending_cfg && !g_pending_restore && !ota_busy_body() && !g_body_owner;
                    if (c->body_to_g) g_body_owner = c;           /* a second POST at the same time must not mix in */
                }
            } else if (c->body_len < c->content_len) {
                if (c->body_to_g) g_body[c->body_len] = d[i];
                c->body_len++;
            }
        }
    }
    pbuf_free(p);
    if (c->header_done && c->body_len >= c->content_len) {
        handle(pcb, c);
        if (g_body_owner == c) g_body_owner = NULL;
    }
    return ERR_OK;
}
static void http_err(void *arg, err_t err) {
    (void)err;
    conn_t *c = (conn_t *)arg;
    if (c) { if (g_body_owner == c) g_body_owner = NULL; free(c->own); free(c); g_conns--; }   /* the pcb is gone already */
}
static err_t http_poll(void *arg, struct tcp_pcb *pcb) {      /* every ~5 s */
    conn_t *c = (conn_t *)arg;
    if (c && c->done && c->tx_pos < c->tx_hdr_len + c->tx_body_len && c->tx_pos != c->poll_pos) {
        c->poll_pos = c->tx_pos;
        send_more(pcb, c);
        return ERR_OK;
    }
    return conn_close(pcb, c) ? ERR_ABRT : ERR_OK;
}
static err_t http_accept(void *arg, struct tcp_pcb *pcb, err_t err) {
    (void)arg;
    if (err != ERR_OK || !pcb) return ERR_VAL;
    conn_t *c = g_conns < 4 ? (conn_t *)calloc(1, sizeof(conn_t)) : NULL;
    if (!c) { tcp_abort(pcb); return ERR_ABRT; }
    g_conns++;
    tcp_arg(pcb, c);
    tcp_recv(pcb, http_recv);
    tcp_sent(pcb, http_sent);
    tcp_err(pcb, http_err);
    tcp_poll(pcb, http_poll, 10);
    return ERR_OK;
}

static void start_servers(void) {
    if (g_servers_up) return;
    cyw43_arch_lwip_begin();
    struct tcp_pcb *l = tcp_new_ip_type(IPADDR_TYPE_ANY);
    if (l && tcp_bind(l, IP_ANY_TYPE, 80) == ERR_OK) {
        l = tcp_listen_with_backlog(l, 4);
        tcp_accept(l, http_accept);
    }
    g_udp = udp_new_ip_type(IPADDR_TYPE_ANY);
    if (g_udp && udp_bind(g_udp, IP_ANY_TYPE, AUDIO_PORT) == ERR_OK) udp_recv(g_udp, audio_recv, NULL);
    cyw43_arch_lwip_end();
    g_servers_up = true;
}

/* ------------------------------------------------------------ connection handling */
static absolute_time_t g_t_retry;                        /* own access point after a failed connect: try the home WLAN again */
static int g_tries;                                      /* failed attempts in a row (NS_CONNECTING) */
static absolute_time_t g_t_noip;                         /* logged in at the router since (0 = not yet in this attempt) */
static void connect_again(void) {                        /* main loop, NS_CONNECTING: leave, then the same WLAN again */
    cyw43_wifi_leave(&cyw43_state, CYW43_ITF_STA);
    cyw43_arch_wifi_connect_async(g_cfg.ssid, g_cfg.pass[0] ? g_cfg.pass : NULL,
                                  g_cfg.pass[0] ? CYW43_AUTH_WPA2_AES_PSK : CYW43_AUTH_OPEN);
    g_t_connect = get_absolute_time(); g_t_noip = 0;
}
static void connect_sta(void) {
    cyw43_arch_enable_sta_mode();
    cyw43_arch_lwip_begin();                             /* the name the router shows (DHCP), before the link is up */
    netif_set_hostname(&cyw43_state.netif[CYW43_ITF_STA], g_hostname);
    cyw43_arch_lwip_end();
    cyw43_wifi_pm(&cyw43_state, CYW43_NONE_PM);          /* see below: no power saving */
    cyw43_arch_wifi_connect_async(g_cfg.ssid, g_cfg.pass[0] ? g_cfg.pass : NULL,
                                  g_cfg.pass[0] ? CYW43_AUTH_WPA2_AES_PSK : CYW43_AUTH_OPEN);
    g_t_connect = get_absolute_time(); g_t_noip = 0; g_tries = 0;
    g_state = NS_CONNECTING;
}
static void start_ap(void) {
    g_t_retry = make_timeout_time_ms(300000);
    cyw43_arch_disable_sta_mode();
    cyw43_arch_enable_ap_mode(g_ap_ssid, AP_PASS, CYW43_AUTH_WPA2_AES_PSK);
    ip_addr_t gw, mask;
    IP4_ADDR(ip_2_ip4(&gw), 192, 168, 4, 1);
    IP4_ADDR(ip_2_ip4(&mask), 255, 255, 255, 0);
    cyw43_arch_lwip_begin();
    dhcp_server_init(&g_dhcp, &cyw43_state.netif[CYW43_ITF_AP], &gw, &mask);
    cyw43_arch_lwip_end();
    strcpy(g_ip, "192.168.4.1");
    g_my_ip = ip4_addr_get_u32(ip_2_ip4(&gw));
    g_state = NS_AP;
    start_servers();
    DIAG("wlan: eigener Zugangspunkt %s (192.168.4.1)", g_ap_ssid);
}

void net_load_settings(void) {
    g_pending_cfg = false; g_pending_restore = false; g_prefs_dirty = false;
    cfg_load();
    prefs_load(&g_prefs);
    if (g_prefs.src >= U64_MAX || !g_cfg.u64[g_prefs.src].addr[0]) {     /* the chosen one was removed: first configured */
        g_prefs.src = 0;
        for (int i = 0; i < U64_MAX; i++) if (g_cfg.u64[i].addr[0]) { g_prefs.src = (uint8_t)i; break; }
    }
}
int  net_latency_ms(void) { return g_cfg.latency_ms; }
int  net_volume(void) { return g_prefs.volume; }
bool net_play_wanted(void) { return g_prefs.play; }

void net_init(void) {
    DIAG("net: init - ssid='%s' quelle=%d '%s' weiterleiten=%d '%s' puffer=%d ms", g_cfg.ssid, g_prefs.src + 1,
         cur_u64()->addr, g_cfg.fwd_on, g_cfg.fwd, g_cfg.latency_ms);
    srclock_reset(&g_src);
    g_state_was_sta = false;
    g_rest = NULL; g_rq = RQ_NONE; memset(g_want, 0, sizeof g_want);   /* nothing in flight (as after a real restart) */
    g_play = g_prefs.play;
    if (g_play) player_play(true);
    if (cyw43_arch_init_with_country(CYW43_COUNTRY_GERMANY)) { DIAG("net: cyw43-Init fehlgeschlagen"); g_state = NS_OFF; return; }
    g_t_pps = get_absolute_time();
    mdnsl_set_name(base_name());
    if (g_cfg.name[0]) snprintf(g_ap_ssid, sizeof g_ap_ssid, "%s-%s", AP_SSID, g_cfg.name); else strcpy(g_ap_ssid, AP_SSID);
    snprintf(g_hostname, sizeof g_hostname, "%s", base_name());
    if (!g_cfg.ssid[0]) { start_ap(); return; }
    connect_sta();
}

int net_led(void) {
    if (g_state == NS_OFF) return NET_LED_OFF;            /* (WLAN chip not started: its LED cannot be used) */
    if (g_state == NS_CONNECTING) return NET_LED_CONNECTING;
    if (g_state == NS_AP) return NET_LED_AP;
    player_stats_t s; player_stats(&s);
    return s.state == JIT_PLAY ? NET_LED_PLAYING : NET_LED_IDLE;
}

/* the stream the Ultimate sent to us stopped although we did not stop it: another device asked for it
   (visualizer, PC jukebox) - or the Ultimate was switched off. Twice within 2 minutes = contested. */
static void watch_takeover(absolute_time_t now) {
    static bool was_direct = false;
    bool d = direct_active();
    if (was_direct && !d && g_play && time_reached(g_quiet_until)) {
        g_taken++;
        g_taken_at[0] = g_taken_at[1]; g_taken_at[1] = to_ms_since_boot(now);
        if (g_taken >= 2 && g_taken_at[1] - g_taken_at[0] < 120000) g_contested = true;
        DIAG("stream: kommt nicht mehr vom Ultimate (%lu. Mal)%s", (unsigned long)g_taken,
             g_contested ? " - ein anderes Gerät fordert ihn immer wieder an" : "");
    }
    was_direct = d;
}

void net_task(void) {
    absolute_time_t now = get_absolute_time();
    diag_task();
    ota_task();

    if (g_pending_cfg) {                                 /* settings from the web page: write, then restart */
        g_newcfg.magic = NETCFG_MAGIC;
        g_newcfg.check = cfg_check(&g_newcfg);
        player_play(false);
        flash_write(NETCFG_OFFSET, (const uint8_t *)&g_newcfg, sizeof g_newcfg);
        if (g_newsrc >= 0 || g_newplay >= 0) {             /* source / on-off from the setup assistant */
            if (g_newsrc >= 0) g_prefs.src = (uint8_t)g_newsrc;
            if (g_newplay >= 0) g_prefs.play = g_newplay == 1;
            g_prefs_dirty = false; prefs_save(&g_prefs);
        }
        watchdog_reboot(0, 0, 100);
        while (1) tight_loop_contents();
    }
    if (g_pending_restore) {                             /* a backup, checked by the route: write, then restart */
        player_play(false);
        g_cfg = g_restcfg;                                   /* RAM copies too: nothing may write the old values back */
        flash_write(NETCFG_OFFSET, (const uint8_t *)&g_cfg, sizeof g_cfg);
        if (g_rest_has_prefs) { g_prefs = g_restprefs; g_prefs_dirty = false; prefs_save(&g_prefs); }
        DIAG("net: Sicherung eingespielt, Neustart");
        watchdog_reboot(0, 0, 100);
        while (1) tight_loop_contents();
    }
    if (g_prefs_dirty && time_reached(g_t_prefs)) {      /* volume / source / on-off: 2 s after the last change */
        g_prefs_dirty = false;
        prefs_save(&g_prefs);
    }
    if (absolute_time_diff_us(g_t_pps, now) >= 1000000) {   /* packets per second for the status page */
        g_pps = g_pkts - g_pkts_prev; g_pkts_prev = g_pkts;
        g_fpps = g_fwd_pkts - g_fwd_prev; g_fwd_prev = g_fwd_pkts;
        g_t_pps = now;
    }

    /* the web routes run in the lwIP context and change the stream state: hold the lwIP lock (recursive) while the
       main loop works with it, so a request is handled before or after this block, never in the middle */
    cyw43_arch_lwip_begin();
    switch (g_state) {
        case NS_CONNECTING: {
            int st = cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA);
            static int st_prev = -99;
            if (st != st_prev) { DIAG("wlan: Verbindungsstatus %d -> %d", st_prev, st); st_prev = st; }
            if (st == CYW43_LINK_UP) {
                const ip4_addr_t *me = netif_ip4_addr(&cyw43_state.netif[CYW43_ITF_STA]);
                snprintf(g_ip, sizeof g_ip, "%s", ip4addr_ntoa(me));
                g_my_ip = ip4_addr_get_u32(me);
                g_state = NS_STA;
                DIAG("wlan: verbunden mit '%s', IP %s", g_cfg.ssid, g_ip);
                start_servers();
                dev_start();                              /* beacons: the device list on every web page */
                /* no WLAN power saving: in power-save mode the router holds back packets for the sleeping chip -
                   with the audio stream (250 packets/s) that builds a queue of seconds (visualizer v36) */
                cyw43_arch_lwip_begin();
                cyw43_wifi_pm(&cyw43_state, CYW43_NONE_PM);
                mdnsl_start(&cyw43_state.netif[CYW43_ITF_STA]);   /* <name>.local, first checks it is free */
                cyw43_arch_lwip_end();
                g_t_req = make_timeout_time_ms(500);
                strcpy(g_u64state, g_play && g_cfg.fed ? "warte auf den weitergeleiteten Stream"
                                   : cur_u64()->addr[0] ? (g_play ? "wird gestartet" : "Wiedergabe aus") : "nicht eingerichtet");
                g_tries = 0;
            } else {
                /* Why it does not work decides what comes next (2026-10-15: after a firmware update the Pico was logged
                   in at the router but got no address for 18 s, went to its own access point and was gone for 5 min):
                   - wrong password: own access point at once (that is where it is corrected) - unless the home WLAN
                     already worked since the start (then it is a hiccup of the router: keep trying)
                   - logged in, no address (DHCP): wait up to 30 s, then leave and connect again
                   - WLAN not found / failed / no progress for 20 s: connect again
                   Own access point only after CONNECT_TRIES attempts and never when the home WLAN already worked. */
                if (st == CYW43_LINK_NOIP && !to_ms_since_boot(g_t_noip)) g_t_noip = now;
                const char *why = st == CYW43_LINK_BADAUTH ? "WLAN-Passwort falsch"
                                : st == CYW43_LINK_NONET ? "WLAN nicht gefunden"
                                : st == CYW43_LINK_FAIL ? "Verbindung fehlgeschlagen"
                                : st == CYW43_LINK_NOIP ? (absolute_time_diff_us(g_t_noip, now) > DHCP_TIMEOUT_MS * 1000ll ? "keine Adresse per DHCP" : NULL)
                                : absolute_time_diff_us(g_t_connect, now) > CONNECT_TIMEOUT_MS * 1000ll ? "keine Antwort vom Router" : NULL;
                if (why && st < 0 && absolute_time_diff_us(g_t_connect, now) < 3000000) why = NULL;   /* quick failures: not faster than every 3 s */
                if (why) {
                    bool worked = g_servers_up && g_state_was_sta;
                    g_tries++;
                    if (!worked && (st == CYW43_LINK_BADAUTH || g_tries >= CONNECT_TRIES)) {
                        DIAG("wlan: %s (Versuch %d) - eigener Zugangspunkt", why, g_tries);
                        start_ap();                             /* home WLAN not usable: own access point */
                    } else {
                        DIAG("wlan: %s (Versuch %d) - verbinde neu", why, g_tries);
                        connect_again();
                    }
                }
            }
            break;
        }
        case NS_AP:
            /* fell back to the own access point although a home WLAN is set (e.g. the router started later after a
               power cut): try it again every 5 minutes, unless someone is using the page on the access point */
            if (g_cfg.ssid[0] && time_reached(g_t_retry)) {
                if (absolute_time_diff_us(g_t_web, now) < 300000000ll && to_ms_since_boot(g_t_web)) { g_t_retry = make_timeout_time_ms(60000); break; }
                DIAG("wlan: versuche das Heim-WLAN erneut");
                dhcp_server_deinit(&g_dhcp);
                cyw43_arch_disable_ap_mode();
                connect_sta();
            }
            break;
        case NS_STA: {
            /* seen once on the test Pico after a restart: WLAN "connected" with an address, but no packet in or out.
               Three unanswered requests to the Ultimate, no stream and no web page use: connect again (at most every
               5 min - an Ultimate that is switched off looks the same) */
            static absolute_time_t last_heal;
            if (g_rest_fail >= 3 && !rx_active() && absolute_time_diff_us(g_t_web, now) > 120000000ll
                && (!to_ms_since_boot(last_heal) || absolute_time_diff_us(last_heal, now) > 300000000ll)) {
                last_heal = now; g_rest_fail = 0;
                DIAG("wlan: keine Antworten im Netz - verbinde neu");
                cyw43_wifi_leave(&cyw43_state, CYW43_ITF_STA);
                g_state_was_sta = true; g_t_connect = now; g_state = NS_CONNECTING;
                cyw43_arch_wifi_connect_async(g_cfg.ssid, g_cfg.pass[0] ? g_cfg.pass : NULL,
                                              g_cfg.pass[0] ? CYW43_AUTH_WPA2_AES_PSK : CYW43_AUTH_OPEN);
                break;
            }
            /* link lost (router restarted, out of range): connect again; the servers keep running */
            if (cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA) != CYW43_LINK_UP) {
                static absolute_time_t down_since; static bool down = false;
                if (!down) { down = true; down_since = now; }
                else if (absolute_time_diff_us(down_since, now) > 10000000) {
                    down = false;
                    DIAG("wlan: Verbindung verloren - verbinde neu");
                    g_state_was_sta = true;
                    g_t_connect = now; g_state = NS_CONNECTING;
                    cyw43_arch_wifi_connect_async(g_cfg.ssid, g_cfg.pass[0] ? g_cfg.pass : NULL,
                                                  g_cfg.pass[0] ? CYW43_AUTH_WPA2_AES_PSK : CYW43_AUTH_OPEN);
                    break;
                }
            }
            static bool named = false;
            if (!named) {
                cyw43_arch_lwip_begin(); mdnsl_task(); cyw43_arch_lwip_end();
                if (mdnsl_ready()) { named = true; DIAG("net: mDNS als %s.local", mdnsl_name()); }
            }
            const u64cfg_t *u = cur_u64();
            res_set(&g_res_u64, u->addr);
            res_set(&g_res_fwd, g_cfg.fwd_on ? g_cfg.fwd : "");
            res_task(&g_res_u64); res_task(&g_res_fwd);
            g_u64_ip = res_u32(&g_res_u64);
            g_fwd_ip = g_cfg.fwd_on ? res_u32(&g_res_fwd) : 0;
            watch_takeover(now);
            npr_task(g_fwd_ip, g_cfg.fwd, g_cfg.fwdpw);   /* title messages of the PC jukebox -> visualizer */
            u64scan_task();                                   /* network search for Ultimates (from the web page) */
            dev_task();                                       /* beacons, answers to queries */
            /* ask for the stream while none arrives - from the Ultimate or forwarded by another device */
            if (g_play && !g_cfg.fed && u->addr[0] && g_res_u64.have && (!rx_active() || g_force_req) && time_reached(g_t_req)) {
                g_force_req = false;
                rest_want(RQ_START, g_res_u64.ip, u->addr, u->pw);
                g_t_req = make_timeout_time_ms(g_contested ? REQ_RETRY_CONTESTED_MS : REQ_RETRY_MS);
            }
            if (g_play && !g_cfg.fed && u->addr[0] && !g_res_u64.have && g_res_u64.failed) strcpy(g_u64state, "Adresse nicht gefunden");
            rest_task();
            /* status line: what is happening now (a reply text such as "Passwort falsch" stays until it changes) */
            if (g_rq == RQ_NONE) {
                if (!g_play) strcpy(g_u64state, u->addr[0] || g_cfg.fed ? "Wiedergabe aus" : "nicht eingerichtet");
                else if (rx_active()) strcpy(g_u64state, direct_active() ? "Stream läuft" : "Stream kommt weitergeleitet");
                else if (!strcmp(g_u64state, "Wiedergabe aus") || !strcmp(g_u64state, "Stream läuft")
                         || !strcmp(g_u64state, "Stream kommt weitergeleitet") || (g_cfg.fed && !strcmp(g_u64state, "wird gestartet")))
                    strcpy(g_u64state, g_cfg.fed ? "warte auf den weitergeleiteten Stream" : "warte auf den Stream");
            }
            break;
        }
        default:
            break;
    }
    cyw43_arch_lwip_end();
}
