/*
 * Network part with fakes of lwIP / cyw43 / flash / player: settings (incl. taking over the visualizer's WLAN),
 * web page + Digest login, stream requests to the Ultimate, receiving, forwarding, source lock, commands,
 * saved volume / source, "now playing" from the PC jukebox, takeover detection.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <setjmp.h>
#include "net.h"
#include "pico/cyw43_arch.h"
#include "player.h"
#include "help_page.h"

struct tcp_pcb { void *arg; int closed; char out[70000]; int outlen; void *recv, *sent, *err; int port; int seen; ip_addr_t remote_ip; uint16_t rcv_wnd, rcv_ann_wnd; };
struct tcp_pcb *test_connect(void); void test_send(struct tcp_pcb *p, const char *d, int split);
void test_udp_from(const unsigned char *pkt, int n, uint32_t from); int test_dns_answer(const char *name, const char *ip);
void test_rest_reply(int i, const char *r); void test_tcp_reply(struct tcp_pcb *p, const char *r); uint32_t IP(const char *s);
extern int64_t FAKE_NOW; extern int FAKE_LINK, REBOOTED, NRESTS, UDP_SENT_N, UDP_SENT_LEN; extern jmp_buf REBOOT_JMP;
extern unsigned char FAKE_FLASH[], UDP_SENT[]; extern ip_addr_t UDP_SENT_TO; extern unsigned short UDP_SENT_PORT; extern struct tcp_pcb *RESTS[];
extern int FP_PACKETS, FP_LAST_SEQ, FP_PLAY, FP_VOL, FP_RESTARTS, FP_FLASH;
extern char FAKE_AP_SSID[]; extern int FAKE_CONNECTS, FAKE_LEAVES;
typedef struct { unsigned short port; uint32_t to; int len; unsigned char d[800]; } udp_log_t;   /* as in fakes/lwip/udp.h */
extern udp_log_t UDP_LOG[256]; extern int UDP_LOG_N; void test_udp_port(unsigned short port, const uint8_t *pkt, int n, uint32_t from);

static int fails = 0;
static void check(int ok, const char *what) { printf("%s %s\n", ok ? "ok  " : "FAIL", what); if (!ok) fails++; }

/* ---- web requests with Digest login (md5.c: checked against the system's md5 in the visualizer's tests) */
static char PW[40] = "", NONCE[64] = ""; static int NC = 0;
#include "md5.h"
static void md5(const char *s, char out[33]) { md5_hex(s, strlen(s), out); }
static struct tcp_pcb *req(const char *method, const char *uri, const char *body) {
    char rq[4000], auth[1000] = "";
    if (PW[0] && NONCE[0]) {
        char ha1[33], ha2[33], r[33], b[1500], nc[12];
        snprintf(nc, sizeof nc, "%08x", ++NC);
        snprintf(b, sizeof b, "admin:C64 Audio:%s", PW); md5(b, ha1);
        snprintf(b, sizeof b, "%s:%s", method, uri); md5(b, ha2);
        snprintf(b, sizeof b, "%s:%s:%s:xyz:auth:%s", ha1, NONCE, nc, ha2); md5(b, r);
        snprintf(auth, sizeof auth, "Authorization: Digest username=\"admin\", realm=\"C64 Audio\", nonce=\"%s\", uri=\"%s\", algorithm=MD5, response=\"%s\", qop=auth, nc=%s, cnonce=\"xyz\"\r\n", NONCE, uri, r, nc);
    }
    snprintf(rq, sizeof rq, "%s %s HTTP/1.1\r\nHost: c64audio.local\r\n%sContent-Length: %d\r\n\r\n%s", method, uri, auth, body ? (int)strlen(body) : 0, body ? body : "");
    struct tcp_pcb *c = test_connect(); test_send(c, rq, 37);
    char *n = strstr(c->out, "nonce=\"");
    if (strstr(c->out, " 401 ") && n) { n += 7; int k = 0; while (n[k] != '"' && k < 60) { NONCE[k] = n[k]; k++; } NONCE[k] = 0; }
    return c;
}
static const char *body_of(struct tcp_pcb *c) { char *b = strstr(c->out, "\r\n\r\n"); return b ? b + 4 : ""; }
static int code_of(struct tcp_pcb *c) { return atoi(c->out + 9); }
static struct tcp_pcb *status(void) { return req("GET", "/status", NULL); }

/* the REST request number i: its request line, Host and password header */
static const char *rest_line(int i) { static char s[300]; int n = 0; while (RESTS[i]->out[n] && RESTS[i]->out[n] != '\r' && n < 299) { s[n] = RESTS[i]->out[n]; n++; } s[n] = 0; return s; }

/* ---- the flash as the visualizer leaves it (netcfg v3) */
static unsigned fnv(const void *d, unsigned long n) { const unsigned char *p = d; unsigned h = 2166136261u; for (unsigned long i = 0; i < n; i++) { h ^= p[i]; h *= 16777619u; } return h; }
typedef struct { unsigned magic; char ssid[33], pass[65], u64[64], u64pw[33], webpw[33]; char name[17]; unsigned char stream, smode, pad; unsigned check; } viz_t;

static unsigned char PKT[770];
static void pkt(uint16_t seq) { PKT[0] = (unsigned char)seq; PKT[1] = (unsigned char)(seq >> 8); for (int i = 2; i < 770; i++) PKT[i] = (unsigned char)(i * 7 + seq); }
static int OKD = 0;                                         /* answer every request not answered yet with 200 */
static void ok_all(void) { extern int NRESTS; for (; OKD < NRESTS; OKD++) if (!RESTS[OKD]->closed) test_rest_reply(OKD, "HTTP/1.1 200 OK\r\n\r\n"); }
static void tick(int ms) { for (int i = 0; i < ms; i += 10) { FAKE_NOW += 10000; net_task(); } }
static void boot(void) { NRESTS = 0; OKD = 0; net_load_settings(); player_init(net_latency_ms(), net_volume()); net_init(); }

/* ---- the network search: these addresses answer (like at home) */
extern int (*FAKE_HOST_UP)(uint32_t ip); extern struct tcp_pcb *SCANS[]; extern int NSCANS;
#include "lwip/etharp.h"
extern struct { uint32_t ip; struct eth_addr mac; } FAKE_ARP[8]; extern int FAKE_NARP;
static void arp(const char *ip, const char *m) { unsigned b[6]; sscanf(m, "%x:%x:%x:%x:%x:%x", &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]);
    FAKE_ARP[FAKE_NARP].ip = IP(ip); for (int i = 0; i < 6; i++) FAKE_ARP[FAKE_NARP].mac.addr[i] = (uint8_t)b[i]; FAKE_NARP++; }
/* Elite II: Ethernet .168 + WLAN .202 (names its wifi_mac); C64 Ultimate: WLAN .103 + Ethernet .246 (no MAC fields, its
   Ethernet MAC starts with 02:15:41); Elite: only on WLAN .138; router .1; .20 takes the connection but never answers */
static const char *ADDR_UP[] = { "192.168.1.168", "192.168.1.202", "192.168.1.103", "192.168.1.246", "192.168.1.138", "192.168.1.1", "192.168.1.20" };
static int host_up(uint32_t ip) { for (int i = 0; i < 7; i++) if (ip == IP(ADDR_UP[i])) return 1; return 0; }
static void answer_scan(void) {
    for (int i = 0; i < NSCANS; i++) {
        struct tcp_pcb *p = SCANS[i]; uint32_t a = p->remote_ip.addr;
        if (p->seen || p->outlen == 0 || !p->recv || !host_up(a) || a == IP("192.168.1.20")) continue;
        p->seen = 1;
        const char *r = a == IP("192.168.1.168") || a == IP("192.168.1.202")
            ? "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n{\n  \"product\" : \"Ultimate 64-II\",\n  \"hostname\" : \"Elite \\\"II\\\"\",\n"
              "  \"unique_id\" : \"ABC123\",\n  \"ethernet_mac\" : \"02:15:41:11:22:33\",\n  \"wifi_mac\" : \"12:34:56:78:9A:BC\",\n  \"errors\" : []\n}"
            : a == IP("192.168.1.103") || a == IP("192.168.1.246") ? "HTTP/1.1 200 OK\r\n\r\n{\"product\":\"C64 Ultimate\",\"hostname\":\"C64U\",\"unique_id\":\"DEF456\"}"
            : a == IP("192.168.1.138") ? "HTTP/1.1 200 OK\r\n\r\n{\"product\":\"Ultimate 64 Elite\",\"hostname\":\"Elite1\",\"unique_id\":\"E1\",\"wifi_mac\":\"10:BF:67:AA:BB:CC\"}"
            : "HTTP/1.1 200 OK\r\n\r\n<html>FRITZ!Box</html>";
        test_tcp_reply(p, r);
    }
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    memset(FAKE_FLASH, 0xFF, 4 * 1024 * 1024);
    /* ---------- 1. a Pico that ran the visualizer: WLAN, web password and Ultimate are taken over */
    viz_t v; memset(&v, 0, sizeof v); v.magic = 0xC64E7C03u; strcpy(v.ssid, "Fritz!Box"); strcpy(v.pass, "wlan-geheim");
    strcpy(v.u64, "192.168.1.168"); strcpy(v.u64pw, "u64pw"); strcpy(v.webpw, "Geheim64"); v.check = fnv(&v, __builtin_offsetof(viz_t, check));
    memcpy(FAKE_FLASH + 4 * 1024 * 1024 - 2 * 4096, &v, sizeof v);
    FAKE_NOW = 1000000; boot();
    FAKE_LINK = CYW43_LINK_UP; cyw43_state.netif[0].ip.addr = IP("192.168.1.50"); tick(20);
    strcpy(PW, "Geheim64");
    struct tcp_pcb *c = status();
    check(code_of(c) == 401 && NONCE[0], "home WLAN: the page asks for the password (Digest)");
    c = status();
    check(code_of(c) == 200, "status with the visualizer's web password");
    const char *s = body_of(c);
    check(strstr(s, "\"ssid\":\"Fritz!Box\"") && strstr(s, "\"addr\":\"192.168.1.168\",\"pw\":true") && strstr(s, "\"fw\":\"c64audio-fw\""),
          "visualizer settings taken over: WLAN, Ultimate + password");
    check(strstr(s, "\"u64\":\"192.168.1.168\"") != NULL, "status has \"u64\" as a plain address (the PC jukebox reads it)");
    tick(600);
    check(NRESTS == 1 && !strcmp(rest_line(0), "PUT /v1/streams/audio:start?ip=192.168.1.50:11001 HTTP/1.1")
          && strstr(RESTS[0]->out, "X-Password: u64pw\r\n") && strstr(RESTS[0]->out, "Host: 192.168.1.168\r\n"),
          "playing by default: asks the Ultimate for the stream (own IP, password header)");
    test_rest_reply(0, "HTTP/1.1 200 OK\r\n\r\n{ \"errors\" : [ ] }");
    tick(3000);
    check(NRESTS == 1, "no second request while the first is fresh");

    /* ---------- 2. settings from the web page: two Ultimates, forwarding to c64viz.local */
    const char *form = "name=Wohnzimmer&ssid=Fritz%21Box&pass=&l0=Elite+II&a0=http%3A%2F%2F192.168.1.168%2F&p0=&x0=0"
                       "&l1=Elite&a1=ultimate1.fritz.box&p1=pw2&x1=0&l2=&a2=&p2=&x2=0&l3=&a3=&p3=&x3=0&fwd_on=1&fwd=c64viz.local&fwdpw=Viz%26Pw1&lat=100&webpw=";
    c = req("POST", "/config", "lat=20");
    check(code_of(c) == 400 && strstr(body_of(c), "40 bis 300"), "buffer below 40 ms refused");
    c = req("POST", "/config", "name=-bad-");
    check(code_of(c) == 400 && strstr(body_of(c), "Gerätename"), "invalid device name refused");
    static struct tcp_pcb *saved;                            /* (static: survives the longjmp of the restart) */
    if (!setjmp(REBOOT_JMP)) { saved = req("POST", "/config", form); tick(10); }
    check(REBOOTED && saved && code_of(saved) == 200, "settings saved, Pico restarts");

    /* ---------- 3. after the restart */
    REBOOTED = 0; NONCE[0] = 0; FAKE_NOW += 1000000; boot(); tick(20);
    c = status(); c = status(); s = body_of(c);
    check(strstr(s, "\"cfgname\":\"wohnzimmer\"") && strstr(s, "\"addr\":\"192.168.1.168\",\"pw\":true") && strstr(s, "\"label\":\"Elite\",\"addr\":\"ultimate1.fritz.box\",\"pw\":true")
          && strstr(s, "\"fwd\":\"c64viz.local\"") && strstr(s, "\"lat\":100") && strstr(s, "\"fwdpw\":true"), "settings after the restart (http://.../ cut off, passwords kept)");
    tick(600);
    check(NRESTS == 1 && strstr(rest_line(0), "audio:start?ip=192.168.1.50:11001"), "stream requested again after the restart");
    test_rest_reply(0, "HTTP/1.1 200 OK\r\n\r\n");
    check(test_dns_answer("c64viz.local", "192.168.1.231") == 1, "forwarding target looked up by name (mDNS via lwIP)");
    tick(20);

    /* ---------- 4. receiving + forwarding */
    int sent0 = UDP_SENT_N, p0 = FP_PACKETS;
    pkt(33623); test_udp_from(PKT, 770, IP("192.168.1.168"));
    check(FP_PACKETS == p0 + 1 && FP_LAST_SEQ == 33623, "packet from the Ultimate goes to the player (sequence number)");
    check(UDP_SENT_N == sent0 + 1 && UDP_SENT_LEN == 770 && !memcmp(UDP_SENT, PKT, 770) && UDP_SENT_TO.addr == IP("192.168.1.231") && UDP_SENT_PORT == 11001,
          "... and is forwarded unchanged to the visualizer (UDP 11001)");
    unsigned char start2[2] = { 0, 0 }; test_udp_from(start2, 2, IP("192.168.1.202"));   /* the Elite II sends it from its WLAN address */
    check(UDP_SENT_N == sent0 + 2 && UDP_SENT_LEN == 2 && FP_PACKETS == p0 + 1, "2-byte start packet (other address of the Ultimate): forwarded, not played");
    pkt(33624); test_udp_from(PKT, 770, IP("192.168.1.168"));
    check(FP_PACKETS == p0 + 2 && UDP_SENT_N == sent0 + 3, "... and it does not lock the source: the stream right after it is taken");
    p0++; sent0++;
    tick(20);
    pkt(1000); test_udp_from(PKT, 770, IP("192.168.1.81"));
    check(FP_PACKETS == p0 + 1 && UDP_SENT_N == sent0 + 2, "second source while the Ultimate streams: ignored (never mix two streams)");
    c = status(); s = body_of(c);
    check(strstr(s, "\"u64state\":\"Stream läuft\"") != NULL, "status line: stream running (also when it was already coming before Start)");
    check(strstr(s, "\"rx\":true") && strstr(s, "\"from\":\"192.168.1.168\"") && strstr(s, "\"direct\":true") && strstr(s, "\"rejected\":1"), "status: stream from the Ultimate, one packet rejected");
    /* the visualizer forwards the stream to us (e.g. it was the receiver of the PC jukebox): play, but never bounce back */
    FAKE_NOW += 600000;
    pkt(2000); test_udp_from(PKT, 770, IP("192.168.1.231"));
    check(FP_PACKETS == p0 + 2 && UDP_SENT_N == sent0 + 2, "packets from the visualizer itself: played, not sent back (no loop)");

    /* ---------- 5. commands */
    c = req("POST", "/cmd?c=vol&v=55", NULL);
    check(code_of(c) == 200 && FP_VOL == 55, "volume 55 %");
    c = req("POST", "/cmd?c=vol&v=101", NULL);
    check(code_of(c) == 400 && FP_VOL == 55, "volume 101 % refused");
    int fl = FP_FLASH; tick(2100);
    check(FP_FLASH == fl + 1, "volume saved 2 s after the last change (audio queued before the flash write)");
    /* playing from the Ultimate directly, then stop: the Ultimate is told to stop */
    for (int i = 0; i < 60; i++) { pkt((uint16_t)(33700 + i)); test_udp_from(PKT, 770, IP("192.168.1.168")); FAKE_NOW += 4000; }
    tick(20);
    int r0 = NRESTS;
    c = req("POST", "/cmd?c=stop", NULL); tick(20);
    check(FP_PLAY == 0 && NRESTS == r0 + 1 && strstr(rest_line(r0), "PUT /v1/streams/audio:stop"), "stop: player stops, the Ultimate stops streaming to us");
    test_rest_reply(r0, "HTTP/1.1 200 OK\r\n\r\n");
    p0 = FP_PACKETS; sent0 = UDP_SENT_N;
    pkt(40000); test_udp_from(PKT, 770, IP("192.168.1.168"));
    check(FP_PACKETS == p0 && UDP_SENT_N == sent0 + 1, "stopped: packets are still forwarded, not played");
    tick(15000);
    check(NRESTS == r0 + 1, "stopped: the stream is not requested again");
    c = req("POST", "/cmd?c=play", NULL); tick(20);
    check(FP_PLAY == 1 && NRESTS == r0 + 2 && strstr(rest_line(r0 + 1), "audio:start"), "play: requested at once");
    test_rest_reply(r0 + 1, "HTTP/1.1 200 OK\r\n\r\n");

    /* ---------- 6. switch to the second Ultimate (a DNS name) */
    for (int i = 0; i < 30; i++) { pkt((uint16_t)(41000 + i)); test_udp_from(PKT, 770, IP("192.168.1.168")); FAKE_NOW += 4000; }
    tick(20);
    r0 = NRESTS; int rs = FP_RESTARTS;
    c = req("POST", "/cmd?c=src&i=1", NULL); tick(20);
    check(code_of(c) == 200 && NRESTS == r0 + 1 && strstr(rest_line(r0), "audio:stop") && RESTS[r0]->remote_ip.addr == IP("192.168.1.168"),
          "other source: the old Ultimate stops first");
    test_rest_reply(r0, "HTTP/1.1 200 OK\r\n\r\n");
    check(FP_RESTARTS == rs + 1, "... the player starts over (fade out, new reserve)");
    check(test_dns_answer("ultimate1.fritz.box", "192.168.1.41") == 1, "... the new one is looked up by name");
    tick(20);
    check(NRESTS == r0 + 2 && strstr(rest_line(r0 + 1), "audio:start") && RESTS[r0 + 1]->remote_ip.addr == IP("192.168.1.41")
          && strstr(RESTS[r0 + 1]->out, "X-Password: pw2\r\n") && strstr(RESTS[r0 + 1]->out, "Host: ultimate1.fritz.box\r\n"),
          "... and is asked for the stream (its own password)");
    test_rest_reply(r0 + 1, "HTTP/1.1 403 Forbidden\r\n\r\n");
    c = status(); check(strstr(body_of(c), "Passwort falsch (403)") != NULL, "wrong Ultimate password shows on the status page");
    c = req("POST", "/cmd?c=src&i=3", NULL);
    check(code_of(c) == 400, "source that is not set up: refused");
    c = req("POST", "/cmd?c=src&i=0", NULL); tick(20);
    test_rest_reply(NRESTS - 1, "HTTP/1.1 200 OK\r\n\r\n");

    /* ---------- 7. the stream is taken by another device (visualizer / PC) - twice: contested, retry every 60 s */
    tick(3000); ok_all();                                    /* (the switch above is more than the quiet time ago) */
    req("POST", "/cmd?c=reset", NULL);                       /* (section 4 already counted one takeover: the forwarded packets) */
    for (int round = 0; round < 2; round++) {
        for (int i = 0; i < 50; i++) { pkt((uint16_t)(50000 + 100 * round + i)); test_udp_from(PKT, 770, IP("192.168.1.168")); FAKE_NOW += 4000; }
        tick(20);
        r0 = NRESTS;
        tick(11000);                                         /* no more packets: someone else has the stream */
        if (getenv("DBG")) { static char d[13000]; extern int diag_snapshot(char *, int); int n = diag_snapshot(d, sizeof d); d[n] = 0; printf("%s\n", n > 1500 ? d + n - 1500 : d); }
        check(NRESTS == r0 + 1 && strstr(rest_line(r0), "audio:start"), round ? "taken again: asked again" : "stream stopped coming: asked again");
        test_rest_reply(r0, "HTTP/1.1 200 OK\r\n\r\n");
    }
    c = status(); s = body_of(c);
    check(strstr(s, "\"taken\":2") && strstr(s, "\"contested\":true"), "taken twice within 2 minutes: contested (shown on the status page)");
    r0 = NRESTS; tick(30000);
    check(NRESTS == r0, "contested: no new request within 30 s");
    tick(31000);
    check(NRESTS == r0 + 1, "contested: next request after 60 s");
    test_rest_reply(r0, "HTTP/1.1 200 OK\r\n\r\n");

    /* ---------- 8. now playing from the PC jukebox */
    c = req("POST", "/jb/np?new=1&title=Commando&author=Rob+Hubbard&secs=300&el=0&song=1&songs=3&sid=D400", NULL);
    check(code_of(c) == 200 && strstr(body_of(c), "\"ok\":true"), "PC jukebox: now playing accepted");
    c = status(); check(strstr(body_of(c), "\"np\":\"Commando \xE2\x80\x93 Rob Hubbard\"") != NULL, "... shown on the status page");
    c = req("POST", "/jb/np?stop=1", NULL); c = status();
    check(strstr(body_of(c), "\"np\":\"\"") != NULL, "... and cleared at stop");
    /* ... and every message goes on to the visualizer, logged in with its password (Digest) */
    {
        int k0 = NRESTS; tick(20);
        int k = -1; for (int i = k0; i < NRESTS; i++) if (RESTS[i]->remote_ip.addr == IP("192.168.1.231")) { k = i; break; }
        const char *np1 = "/jb/np?new=1&title=Commando&author=Rob+Hubbard&secs=300&el=0&song=1&songs=3&sid=D400";
        char want[600]; snprintf(want, sizeof want, "POST %s HTTP/1.1", np1);
        check(k >= 0 && !strcmp(rest_line(k), want) && !strstr(RESTS[k]->out, "Authorization"), "title relay: same request to the visualizer, first without login");
        if (k >= 0) test_rest_reply(k, "HTTP/1.1 401 Unauthorized\r\nWWW-Authenticate: Digest realm=\"C64 Visualizer\", qop=\"auth\", algorithm=MD5, nonce=\"abcdef0123456789\"\r\nContent-Length: 0\r\n\r\n");
        tick(20);
        int k2 = NRESTS - 1; char *a = strstr(RESTS[k2]->out, "Authorization: Digest ");
        char cn[40] = "", nc[16] = "", resp[40] = "", b[800], ha1[33], ha2[33], exp[33];
        if (a) { char *p = strstr(a, "cnonce=\""); if (p) sscanf(p + 8, "%39[^\"]", cn); p = strstr(a, " nc="); if (p) sscanf(p + 4, "%15[^,]", nc);
                 p = strstr(a, "response=\""); if (p) sscanf(p + 10, "%39[^\"]", resp); }
        snprintf(b, sizeof b, "c64audio:C64 Visualizer:Viz&Pw1"); md5(b, ha1);
        snprintf(b, sizeof b, "POST:%s", np1); md5(b, ha2);
        snprintf(b, sizeof b, "%s:abcdef0123456789:%s:%s:auth:%s", ha1, nc, cn, ha2); md5(b, exp);
        check(k2 > k && a && !strcmp(resp, exp) && strstr(a, "uri=\"/jb/np?new=1&title=Commando") && strstr(a, "realm=\"C64 Visualizer\""),
              "... answers the challenge with the visualizer's password (checked independently)");
        test_rest_reply(k2, "HTTP/1.1 200 OK\r\n\r\n{\"ok\":true}");
        k0 = NRESTS; tick(20);
        check(NRESTS == k0 + 1 && strstr(rest_line(k0), "/jb/np?stop=1") && strstr(RESTS[k0]->out, "nc=00000002"), "... then the next one (stop), with the same login");
        test_rest_reply(k0, "HTTP/1.1 409 Conflict\r\n\r\n{\"ok\":false}"); tick(100);
        check(NRESTS == k0 + 1, "visualizer busy (409): waits a moment ...");
        tick(400);
        check(NRESTS == k0 + 2 && strstr(rest_line(k0 + 1), "/jb/np?stop=1"), "... and sends it again");
        test_rest_reply(k0 + 1, "HTTP/1.1 200 OK\r\n\r\n"); tick(20);
        c = status(); check(strstr(body_of(c), "\"npr\":\"übergeben\",\"nprsent\":2") != NULL, "status: 2 messages handed over");
        req("POST", "/jb/np?new=1&title=X&author=Y", NULL); k0 = NRESTS; tick(20);
        test_rest_reply(k0, "HTTP/1.1 401 Unauthorized\r\nWWW-Authenticate: Digest realm=\"C64 Visualizer\", qop=\"auth\", nonce=\"ffff\"\r\n\r\n"); tick(20);
        c = status(); check(strstr(body_of(c), "\"npr\":\"Passwort falsch\"") != NULL && NRESTS == k0 + 1, "wrong password: shown, message dropped (no endless retries)");
    }

    /* ---------- 9. volume / source / on-off survive a restart */
    req("POST", "/cmd?c=vol&v=33", NULL); req("POST", "/cmd?c=stop", NULL); tick(2100);
    REBOOTED = 0; FAKE_NOW += 1000000; boot();
    check(net_volume() == 33 && !net_play_wanted(), "after a restart: volume 33 %, stopped");
    c = req("POST", "/fw/begin?size=100000", NULL);
    c = req("POST", "/cmd?c=play", NULL);
    check(FP_PLAY != 1, "no playback while a firmware update is being received");
    /* many saves: the sector fills and is erased - the newest value always wins */
    for (int i = 0; i < 300; i++) { char u[40]; snprintf(u, sizeof u, "/cmd?c=vol&v=%d", i % 101); req("POST", u, NULL); tick(2100); }
    REBOOTED = 0; FAKE_NOW += 1000000; boot();
    check(net_volume() == 299 % 101, "300 saves (sector erased in between): the last value is loaded");

    /* ---------- 9b. settings of the first firmware (netcfg v1, without the visualizer's password) are kept */
    {
        typedef struct { char label[17]; char addr[64]; char pw[33]; } u_t;
        typedef struct { unsigned magic; char ssid[33], pass[65], webpw[33]; char name[17]; u_t u64[4]; char fwd[64]; unsigned char fwd_on, pad; unsigned short lat; unsigned check; } v1_t;
        v1_t o; memset(&o, 0, sizeof o); o.magic = 0xA0D1C001u; strcpy(o.ssid, "MeinWLAN"); strcpy(o.webpw, "Geheim64"); strcpy(o.u64[0].addr, "192.168.1.168");
        strcpy(o.fwd, "c64viz.local"); o.fwd_on = 1; o.lat = 120; o.check = fnv(&o, __builtin_offsetof(v1_t, check));
        memset(FAKE_FLASH + 4 * 1024 * 1024 - 2 * 4096, 0xFF, 4096); memcpy(FAKE_FLASH + 4 * 1024 * 1024 - 2 * 4096, &o, sizeof o);
        NONCE[0] = 0; FAKE_NOW += 1000000; boot(); tick(20); c = status(); c = status(); s = body_of(c);
        check(strstr(s, "\"ssid\":\"MeinWLAN\"") && strstr(s, "\"fwd\":\"c64viz.local\"") && strstr(s, "\"lat\":120") && strstr(s, "\"fwdpw\":false"),
              "settings of the first firmware taken over (WLAN, forwarding, buffer)");
    }

    /* ---------- 9c. network search for Ultimates (web page: "Netzwerk nach Ultimates durchsuchen") */
    {
        int r0 = NRESTS;
        c = req("POST", "/u64/scan", NULL);
        check(code_of(c) == 200 && strstr(body_of(c), "\"running\":true"), "search started");
        c = status(); check(strstr(body_of(c), "\"scanning\":true") != NULL, "status: searching");
        FAKE_HOST_UP = host_up;
        arp("192.168.1.168", "02:15:41:11:22:33"); arp("192.168.1.202", "12:34:56:78:9a:bc"); arp("192.168.1.103", "3c:dc:75:65:43:21");
        arp("192.168.1.246", "02:15:41:44:55:66"); arp("192.168.1.138", "10:bf:67:aa:bb:cc");
        int64_t t0 = FAKE_NOW;
        for (int t = 0; t < 120000 && strstr(body_of(req("GET", "/u64/scan", NULL)), "\"running\":true"); t += 20) { tick(20); answer_scan(); }
        FAKE_HOST_UP = NULL;
        const char *r = body_of(req("GET", "/u64/scan", NULL)); printf("   %s\n   took %.1f s (simulated)\n", r, (FAKE_NOW - t0) / 1e6);
        check(strstr(r, "\"running\":false,\"done\":254") != NULL, "all 254 addresses done");
        check(strstr(r, "{\"ip\":\"192.168.1.168\",\"alt\":\"192.168.1.202\",\"link\":\"eth\",\"name\":\"Elite  II \",\"product\":\"Ultimate 64-II\"}") != NULL,
              "Elite II once: Ethernet address, WLAN as the other one (quotes in the name made safe)");
        check(strstr(r, "{\"ip\":\"192.168.1.246\",\"alt\":\"192.168.1.103\",\"link\":\"eth\",\"name\":\"C64U\",\"product\":\"C64 Ultimate\"}") != NULL,
              "C64 Ultimate once: Ethernet (found second, MAC 02:15:41) preferred over WLAN");
        check(strstr(r, "{\"ip\":\"192.168.1.138\",\"alt\":\"\",\"link\":\"wlan\"") != NULL, "Elite found only over WLAN: marked as WLAN");
        check(!strstr(r, "\"192.168.1.1\"") && !strstr(r, "192.168.1.20\""), "router and a silent device are no Ultimates");
        int own = 0, done = 0; for (int i = 0; i < NSCANS; i++) { own += SCANS[i]->remote_ip.addr == IP("192.168.1.50"); done += SCANS[i]->closed; }
        check(NSCANS == 253 && own == 0 && done == 253, "253 addresses asked (not our own), every connection closed");
        check(FAKE_NOW - t0 < 90000000, "done within 1.5 minutes (3 addresses at a time)");
        check(NRESTS == r0 || !strstr(rest_line(NRESTS - 1), "/v1/info"), "the search does not disturb the REST requests");
        c = status(); check(strstr(body_of(c), "\"scanning\":false") != NULL, "status: search over");
    }

    /* ---------- 9d. backup and restore of the settings */
    {
        static char form[4000];                                  /* (static: survives the longjmp of the restart) */
        req("POST", "/cmd?c=vol&v=44", NULL); tick(2100);
        c = req("GET", "/backup", NULL); const char *j = body_of(c);
        const char *pn = strstr(j, "\"netcfg\":\""), *pp = strstr(j, "\"prefs\":\"");
        int nlen = pn ? (int)(strchr(pn + 10, '"') - (pn + 10)) : 0, plen = pp ? (int)(strchr(pp + 9, '"') - (pp + 9)) : 0;
        check(code_of(c) == 200 && strstr(j, "{\"fmt\":\"c64audio-backup\",\"ver\":1,") && strstr(j, "\"name\":\"c64audio\"")
              && pn && !strncmp(pn + 10, "02c0d1a0", 8) && nlen > 1000 && plen == 32, "backup: own format, name, settings block (magic) and one prefs record");
        snprintf(form, sizeof form, "netcfg=%.*s&prefs=%.*s&reboot=1", nlen, pn + 10, plen, pp + 9);
        /* other settings on this Pico now */
        if (!setjmp(REBOOT_JMP)) { req("POST", "/config", "name=anders&lat=200"); tick(10); }
        REBOOTED = 0; NONCE[0] = 0; FAKE_NOW += 1000000; boot(); tick(20);
        status(); req("POST", "/cmd?c=vol&v=10", NULL); tick(2100);                  /* (the first request fetches a new nonce) */
        c = status(); check(strstr(body_of(c), "\"cfgname\":\"anders\"") && strstr(body_of(c), "\"lat\":200") && net_volume() == 10, "settings changed afterwards");
        static unsigned char before[2 * 4096]; memcpy(before, FAKE_FLASH + 4 * 1024 * 1024 - 2 * 4096, sizeof before);
        char *f = strdup(form); char *h = strstr(f, "netcfg=") + 7 + 100; *h = *h == '0' ? '1' : '0';    /* one digit changed */
        c = req("POST", "/restore", f); check(code_of(c) == 400 && strstr(body_of(c), "WLAN"), "netcfg changed by one digit: refused");
        f = strdup(form); h = strstr(f, "prefs=") + 6 + 9; *h = *h == '0' ? '1' : '0';
        c = req("POST", "/restore", f); check(code_of(c) == 400 && strstr(body_of(c), "Lautstärke"), "prefs record changed: refused");
        f = strdup(form); h = strstr(f, "netcfg=") + 7; memcpy(h, "05", 2);                               /* visualizer magic 0xC64E7C05 */
        memcpy(h + 2, "7c4ec6", 6); c = req("POST", "/restore", f); check(code_of(c) == 400, "visualizer settings block: refused");
        c = req("POST", "/restore", "hello=1"); check(code_of(c) == 400, "no backup at all: refused");
        tick(20);
        check(!memcmp(before, FAKE_FLASH + 4 * 1024 * 1024 - 2 * 4096, sizeof before) && !REBOOTED, "flash untouched, no restart");
        static int restarted; restarted = 0;
        if (!setjmp(REBOOT_JMP)) { c = req("POST", "/restore", form); check(code_of(c) == 200 && strstr(body_of(c), "startet"), "backup accepted"); tick(10); }
        else restarted = 1;
        check(restarted && REBOOTED, "restored: the Pico restarts");
        REBOOTED = 0; NONCE[0] = 0; FAKE_NOW += 1000000; boot(); tick(20);
        c = status(); c = status(); s = body_of(c);
        check(strstr(s, "\"cfgname\":\"\"") && strstr(s, "\"lat\":120") && strstr(s, "\"ssid\":\"MeinWLAN\"") && strstr(s, "\"fwd\":\"c64viz.local\"")
              && net_volume() == 44, "after the restart: the backed-up settings and volume");
    }

    /* ---------- 9e. connecting to the home WLAN: what a failure leads to */
    {
        /* logged in at the router, but no address for 35 s (seen after a firmware update): connect again, no access point */
        FAKE_LINK = CYW43_LINK_JOIN; FAKE_AP_SSID[0] = 0; FAKE_NOW += 1000000; boot();
        int c0 = FAKE_CONNECTS, l0 = FAKE_LEAVES;
        tick(2000); FAKE_LINK = CYW43_LINK_NOIP; tick(25000);
        check(FAKE_CONNECTS == c0 && !FAKE_AP_SSID[0], "no address after 25 s: still waiting (DHCP may be slow), no access point");
        tick(10000);
        check(FAKE_CONNECTS == c0 + 1 && FAKE_LEAVES == l0 + 1 && !FAKE_AP_SSID[0], "no address after 30 s: leaves and connects again - no access point");
        FAKE_LINK = CYW43_LINK_UP; tick(100);
        c = status(); c = status(); check(strstr(body_of(c), "\"sta\":true") != NULL, "... and then it is in the home WLAN");
        /* the same three times in a row: own access point (the page must stay reachable somehow) */
        FAKE_LINK = CYW43_LINK_NOIP; FAKE_AP_SSID[0] = 0; FAKE_NOW += 1000000; boot(); c0 = FAKE_CONNECTS;
        tick(31000); tick(31000);
        check(FAKE_CONNECTS == c0 + 2 && !FAKE_AP_SSID[0], "no address twice: connects again");
        tick(31000);
        check(!strcmp(FAKE_AP_SSID, "C64-Audio") && FAKE_CONNECTS == c0 + 2, "third time: own access point");
        /* wrong password: own access point at once */
        FAKE_LINK = CYW43_LINK_BADAUTH; FAKE_AP_SSID[0] = 0; FAKE_NOW += 1000000; boot(); c0 = FAKE_CONNECTS;
        tick(4000);
        check(!strcmp(FAKE_AP_SSID, "C64-Audio") && FAKE_CONNECTS == c0, "wrong WLAN password: own access point without further tries");
        /* WLAN not there: three tries, then the access point */
        FAKE_LINK = CYW43_LINK_NONET; FAKE_AP_SSID[0] = 0; FAKE_NOW += 1000000; boot(); c0 = FAKE_CONNECTS;
        tick(7000);
        check(FAKE_CONNECTS == c0 + 2 && !FAKE_AP_SSID[0], "WLAN not found: tries again (not faster than every 3 s)");
        tick(5000);
        check(!strcmp(FAKE_AP_SSID, "C64-Audio") && FAKE_CONNECTS == c0 + 2, "not found three times: own access point");
        /* not found once (router restarting), then there */
        FAKE_LINK = CYW43_LINK_NONET; FAKE_AP_SSID[0] = 0; FAKE_NOW += 1000000; boot();
        tick(3500); FAKE_LINK = CYW43_LINK_UP; tick(100);
        check(!FAKE_AP_SSID[0] && strstr(body_of(status()), "\"sta\":true"), "not found once, then there: home WLAN, no access point");
        /* the home WLAN worked since the start, then the router is gone for a while: never the access point */
        tick(1000); FAKE_LINK = CYW43_LINK_NONET; FAKE_AP_SSID[0] = 0; tick(120000);
        check(!FAKE_AP_SSID[0], "home WLAN worked before: 2 minutes without it, still no access point");
        FAKE_LINK = CYW43_LINK_UP; tick(4000);
        check(strstr(body_of(status()), "\"sta\":true") != NULL, "... and back when the router is");
        c = req("GET", "/diag", NULL); check(strstr(body_of(c), "keine Adresse per DHCP (Versuch 1) - verbinde neu") && strstr(body_of(c), "WLAN-Passwort falsch (Versuch 1) - eigener Zugangspunkt"),
              "diagnostics name the case");
    }

    /* ---------- 9f. devices in the network (devices.c, same format as the visualizer) and titles without a password */
    {
        static struct tcp_pcb *sv;                               /* (static: survives the longjmp of the restart) */
        if (!setjmp(REBOOT_JMP)) { sv = req("POST", "/config", "name=keller&fwd_on=1&fwd=192.168.1.231&xfwdpw=1&lat=80"); tick(10); }
        check(REBOOTED && sv && code_of(sv) == 200, "forwarding to an IP, visualizer password removed: restart");
        REBOOTED = 0; NONCE[0] = 0; FAKE_NOW += 1000000; boot(); tick(600); ok_all();
        status(); req("POST", "/fw/chunk?off=1", "x");             /* ends the update left open in 9. (no playback during one) */
        req("POST", "/cmd?c=play", NULL); tick(1000);
        for (int i = 0; i < 300; i++) { pkt((uint16_t)(100 + i)); test_udp_from(PKT, 770, IP("192.168.1.168")); FAKE_NOW += 4000; if (i % 25 == 0) net_task(); }
        int l0 = UDP_LOG_N;
        c = req("POST", "/devices/query", NULL); c = req("POST", "/devices/query", NULL); tick(100);
        int nq = 0, nb = 0; const udp_log_t *b = NULL;
        for (int i = l0; i < UDP_LOG_N; i++) { const udp_log_t *l = &UDP_LOG[i % 256]; if (l->port != 11064 || l->to != 0xFFFFFFFFu) continue;
            if (l->len == 8 && !memcmp(l->d, "C64DEV1?", 8)) nq++; else if (!memcmp(l->d, "C64DEV1 {", 9)) { nb++; b = l; } }
        check(code_of(c) == 200 && nq == 1 && nb == 1, "\"Neu suchen\": a query to everybody and its own beacon (broadcast, UDP 11064)");
        char bj[800] = ""; if (b) { memcpy(bj, b->d + 8, (size_t)b->len - 8); bj[b->len - 8] = 0; }
        printf("   beacon: %s\n", bj);
        check(!strncmp(bj, "{\"t\":\"audio\",\"n\":\"keller\",\"ip\":\"192.168.1.50\",\"b\":\"", 49) && strstr(bj, "\",\"img\":\"c64audio\",\"up\":"),
              "beacon: type, name, address, build, firmware file");
        check(strstr(bj, "\"st\":1,\"sm\":0,\"rx\":") && !strstr(bj, "\"rx\":0,") && strstr(bj, "\"from\":\"192.168.1.168\",\"fw\":\"192.168.1.231\",\"fwip\":\"192.168.1.231\",\"fp\":")
              && strstr(bj, "\"src\":\"192.168.1.168\",\"srcl\":\"\"}"), "beacon: playing, stream from the Ultimate, forwarding, source");
        int l1 = UDP_LOG_N; tick(9000);
        int n1 = 0; for (int i = l1; i < UDP_LOG_N; i++) n1 += UDP_LOG[i % 256].port == 11064;
        tick(2000);
        int n2 = 0; for (int i = l1; i < UDP_LOG_N; i++) n2 += UDP_LOG[i % 256].port == 11064;
        check(n1 == 0 && n2 == 1, "next beacon after about 10 s (not before 9.5 s)");
        #define BEACON(ip, j) do { char bb[600]; int bn = snprintf(bb, sizeof bb, "C64DEV1 %s", j); test_udp_port(11064, (const uint8_t *)bb, bn, IP(ip)); } while (0)
        BEACON("192.168.1.231", "{\"t\":\"viz\",\"n\":\"c64viz\",\"ip\":\"192.168.1.231\",\"from\":\"192.168.1.50\",\"rx\":250}");
        BEACON("192.168.1.80", "{\"t\":\"viz\",\"n\":\"bad\"");                    /* not one JSON object */
        BEACON("192.168.1.81", "{\"t\":\"viz\",\"n\":\"ctl\x01\"}");              /* control character */
        BEACON("192.168.1.50", "{\"t\":\"viz\",\"n\":\"me-again\"}");             /* our own address */
        c = req("GET", "/devices", NULL); const char *d = body_of(c);
        printf("   /devices: %.200s\n", d);
        check(code_of(c) == 200 && !strncmp(d, "{\"list\":[{\"self\":true,\"age\":0,\"j\":\"{\\\"t\\\":\\\"audio\\\",\\\"n\\\":\\\"keller\\\"", 60),
              "GET /devices: this receiver first (as JSON string)");
        check(strstr(d, "\\\"n\\\":\\\"c64viz\\\"") && !strstr(d, "bad") && !strstr(d, "ctl") && !strstr(d, "me-again"), "the visualizer is in it; broken beacons and our own address are not");
        int l2 = UDP_LOG_N; test_udp_port(11064, (const uint8_t *)"C64DEV1?", 8, IP("192.168.1.231")); tick(30);
        int n3 = 0; for (int i = l2; i < UDP_LOG_N; i++) n3 += UDP_LOG[i % 256].port == 11064; tick(470);
        int n4 = 0; for (int i = l2; i < UDP_LOG_N; i++) n4 += UDP_LOG[i % 256].port == 11064;
        check(n3 == 0 && n4 == 1, "a query from another device: answered after 50-400 ms");
        tick(36000);
        d = body_of(req("GET", "/devices", NULL)); check(!strstr(d, "c64viz") && strstr(d, "keller"), "a device silent for 35 s is forgotten");
        for (int i = 0; i < 20; i++) { char ip[20], j[60]; snprintf(ip, sizeof ip, "192.168.1.%d", 100 + i); snprintf(j, sizeof j, "{\"t\":\"viz\",\"n\":\"dev%d\"}", i); BEACON(ip, j); }
        d = body_of(req("GET", "/devices", NULL)); int ne = 0; for (const char *q = d; (q = strstr(q, "\"age\"")); q++) ne++;
        check(ne == 13, "20 devices: this one + the 12 heard of last");
        /* a title of the PC jukebox without the visualizer's password: along the stream chain (C64NP1 to UDP 11001) */
        int k0 = NRESTS, l3 = UDP_LOG_N;
        c = req("POST", "/jb/np?new=1&title=Delta&author=Rob+Hubbard&secs=300", NULL); tick(50);
        const udp_log_t *t = NULL; for (int i = l3; i < UDP_LOG_N; i++) if (UDP_LOG[i % 256].port == 11001 && !memcmp(UDP_LOG[i % 256].d, "C64NP1 ", 7)) t = &UDP_LOG[i % 256];
        int restviz = 0; for (int i = k0; i < NRESTS; i++) restviz += RESTS[i]->remote_ip.addr == IP("192.168.1.231");
        check(code_of(c) == 200 && t && t->to == IP("192.168.1.231") && t->len == 7 + 45 && !memcmp(t->d + 7, "new=1&title=Delta&author=Rob+Hubbard&secs=300", 45) && !restviz,
              "no password: the title goes as \"C64NP1 <query>\" to the visualizer's stream port, no web request");
        c = status(); check(strstr(body_of(c), "\"np\":\"Delta \xE2\x80\x93 Rob Hubbard\"") && strstr(body_of(c), "\"npr\":\"gesendet (ohne Passwort)\",\"nprsent\":1"), "status: title shown, sent without password");
        /* such a title arriving here (a visualizer in front of this receiver): shown and passed on like the stream */
        const char *rel = "C64NP1 new=1&title=Cybernoid&author=Jeroen+Tel"; int sent1 = UDP_SENT_N, bad0 = 0;
        { const char *q = strstr(body_of(status()), "\"bad\":"); bad0 = q ? atoi(q + 6) : -1; }
        test_udp_from((const unsigned char *)rel, (int)strlen(rel), IP("192.168.1.168"));
        c = status(); const char *q = strstr(body_of(c), "\"bad\":");
        check(strstr(body_of(c), "\"np\":\"Cybernoid \xE2\x80\x93 Jeroen Tel\"") && UDP_SENT_N == sent1 + 1 && UDP_SENT_LEN == (int)strlen(rel) && q && atoi(q + 6) == bad0,
              "C64NP1 from the stream: title shown, forwarded unchanged, not counted as a broken packet");
    }

    /* ---------- 9g. setup assistant of the visualizer (#setup=): settings with a new source and playing on, then the
       beacon must show exactly what the assistant checks: st 1, fw = wanted target, src = the Ultimate */
    {
        static struct tcp_pcb *sv;
        status(); req("POST", "/cmd?c=stop", NULL); tick(2100); ok_all(); tick(20); ok_all(); tick(20);   /* (no request to the Ultimate left open) */
        c = req("POST", "/config", "a3=&src=3"); check(code_of(c) == 400 && strstr(body_of(c), "Quelle"), "source in an empty row: refused");
        if (!setjmp(REBOOT_JMP)) { sv = req("POST", "/config", "l2=Elite&a2=192.168.1.41&p2=&x2=0&fwd_on=1&fwd=c64viz.local&src=2&play=1"); tick(10); }
        check(REBOOTED && sv && code_of(sv) == 200, "assistant's settings saved (new Ultimate as source, playing on)");
        REBOOTED = 0; NONCE[0] = 0; FAKE_NOW += 1000000; boot(); tick(600);
        check(net_play_wanted(), "after the restart: playing");
        c = status(); c = status(); s = body_of(c);
        check(strstr(s, "\"src\":2,") && strstr(s, "\"play\":true"), "status: source 3 (Elite), playing");
        req("POST", "/devices/query", NULL); tick(100);
        const char *d = body_of(req("GET", "/devices", NULL));
        printf("   self: %.330s\n", d);
        check(strstr(d, "\\\"st\\\":1,") && strstr(d, "\\\"fw\\\":\\\"c64viz.local\\\"") && strstr(d, "\\\"src\\\":\\\"192.168.1.41\\\",\\\"srcl\\\":\\\"Elite\\\""),
              "beacon: st 1, fw c64viz.local, src = the Ultimate's address");
        check(NRESTS > 0 && strstr(rest_line(NRESTS - 1), "audio:start") && strstr(RESTS[NRESTS - 1]->out, "Host: 192.168.1.41"), "asks the new Ultimate for the stream");
        if (!setjmp(REBOOT_JMP)) { sv = req("POST", "/config", "fwd_on=0&fwd=&src=2&play=1"); tick(10); }
        REBOOTED = 0; NONCE[0] = 0; FAKE_NOW += 1000000; boot(); tick(600); status();
        req("POST", "/devices/query", NULL); tick(100); d = body_of(req("GET", "/devices", NULL));
        check(strstr(d, "\\\"st\\\":1,") && strstr(d, "\\\"fw\\\":\\\"\\\",") , "last device of the chain: no forwarding, fw \"\"");
    }

    /* ---------- 9g. "Stream kommt weitergeleitet": the Pi jukebox delivers - never ask the Ultimate, also when it stalls */
    {
        static struct tcp_pcb *sv;
        if (!setjmp(REBOOT_JMP)) { sv = req("POST", "/config", "fed=1&src=2&play=1"); tick(10); }
        check(REBOOTED && sv && code_of(sv) == 200, "\"weitergeleitet\" saved: restart");
        REBOOTED = 0; NONCE[0] = 0; FAKE_NOW += 1000000; boot(); tick(600); ok_all();
        int r0 = NRESTS;
        req("POST", "/cmd?c=play", NULL); tick(15000);
        c = status(); s = body_of(c);
        check(NRESTS == r0 && strstr(s, "\"fed\":true") && strstr(s, "warte auf den weitergeleiteten Stream"),
              "no packets for 15 s: waits, never asks the Ultimate");
        for (int i = 0; i < 300; i++) { pkt((uint16_t)(700 + i)); test_udp_from(PKT, 770, IP("192.168.1.78")); FAKE_NOW += 4000; if (i % 25 == 0) net_task(); }
        c = status(); s = body_of(c);
        check(strstr(s, "\"rx\":true") && strstr(s, "Stream kommt weitergeleitet"), "the Pi's packets play");
        tick(20000);
        check(NRESTS == r0, "the Pi stalls for 20 s: still no request (the stream stays with the Pi)");
        req("POST", "/devices/query", NULL); tick(100);
        const char *d = body_of(req("GET", "/devices", NULL));
        check(strstr(d, "\\\"sm\\\":2,") != NULL, "beacon: sm 2 like a visualizer in mode \"weitergeleitet\"");
        if (!setjmp(REBOOT_JMP)) { sv = req("POST", "/config", "fed=0&src=2&play=1"); tick(10); }
        REBOOTED = 0; NONCE[0] = 0; FAKE_NOW += 1000000; boot(); tick(600); ok_all();
        r0 = NRESTS; req("POST", "/cmd?c=play", NULL); tick(12000);
        check(NRESTS > r0 && strstr(rest_line(NRESTS - 1), "audio:start"), "switched off again: asks as before");
    }

    /* ---------- 10. fresh Pico: own access point, page without password */
    memset(FAKE_FLASH, 0xFF, 4 * 1024 * 1024); PW[0] = 0; NONCE[0] = 0; FAKE_LINK = 0; boot();
    c = req("GET", "/", NULL);
    check(code_of(c) == 200 && strstr(c->out, "C64 AUDIO-EMPF") && !strcmp(FAKE_AP_SSID, "C64-Audio"), "no settings: access point C64-Audio, page without password");
    c = status(); check(strstr(body_of(c), "\"vol\":80") && strstr(body_of(c), "\"lat\":80"), "defaults: volume 80 %, buffer 80 ms");
    c = req("GET", "/diag", NULL); check(code_of(c) == 200 && strstr(body_of(c), "net: init"), "diagnostics log");
    /* the manual for the "Hilfe" tab: the gzip bytes unchanged, with Content-Encoding */
    c = req("GET", "/hilfe", NULL);
    { const char *b = body_of(c);
      check(code_of(c) == 200 && strstr(c->out, "Content-Encoding: gzip\r\n") && c->out + c->outlen - b == HELP_GZ_LEN
            && !memcmp(b, HELP_GZ, HELP_GZ_LEN) && (unsigned char)b[0] == 0x1f && (unsigned char)b[1] == 0x8b, "GET /hilfe: manual as gzip"); }
    c = req("GET", "/nope", NULL); check(code_of(c) == 404, "unknown page: 404");
    c = req("POST", "/u64/scan", NULL); check(code_of(c) == 409 && strstr(body_of(c), "Heim-WLAN"), "own access point: no network search");

    printf(fails ? "\n%d FAILED\n" : "\nall ok\n", fails);
    return fails != 0;
}
