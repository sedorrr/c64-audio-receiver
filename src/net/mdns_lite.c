/* Minimal mDNS responder for "<name>.local" - see mdns_lite.h (RFC 6762) */
#include <string.h>
#include <stdio.h>
#include "pico/stdlib.h"
#include "lwip/udp.h"
#include "lwip/igmp.h"
#include "lwip/ip4_addr.h"
#include "lwip/netif.h"
#include "mdns_lite.h"

#define MDNS_PORT 5353
#define TYPE_A    1
#define TYPE_AAAA 28
#define TYPE_NSEC 47
#define TYPE_ANY  255
#ifndef MDNSL_DEFAULT_NAME
#define MDNSL_DEFAULT_NAME "c64viz"                    /* the build can set its own (-DMDNSL_DEFAULT_NAME=...) */
#endif
#define DEFAULT_NAME MDNSL_DEFAULT_NAME
static char m_base[MDNSL_NAME_MAX + 1] = DEFAULT_NAME;
static char m_name[MDNSL_NAME_MAX + 4] = DEFAULT_NAME;   /* in use: base or base-2 .. base-9 */
static const char *LABELS[2] = { m_name, "local" };

bool mdnsl_name_valid(const char *s) {
    int n = (int)strlen(s);
    if (n < 1 || n > MDNSL_NAME_MAX || s[0] == '-' || s[n - 1] == '-') return false;
    for (int i = 0; i < n; i++)
        if (!((s[i] >= 'a' && s[i] <= 'z') || (s[i] >= '0' && s[i] <= '9') || s[i] == '-')) return false;
    return true;
}
void mdnsl_set_name(const char *base) {
    strcpy(m_base, base && mdnsl_name_valid(base) ? base : DEFAULT_NAME);
    strcpy(m_name, m_base);
}
const char *mdnsl_name(void) { return m_name; }

static bool label_eq(const uint8_t *a, const char *b, int n) {
    if ((int)strlen(b) != n) return false;
    for (int i = 0; i < n; i++) {
        uint8_t c = a[i]; if (c >= 'A' && c <= 'Z') c += 32;
        if (c != (uint8_t)b[i]) return false;
    }
    return true;
}
/* is the (possibly compressed) name at *pos "c64viz.local"? advances *pos behind it; -1 = broken message */
static int name_is_ours(const uint8_t *m, int len, int *pos) {
    int p = *pos, label = 0, jumps = 0; bool ok = true, jumped = false;
    while (p < len) {
        uint8_t l = m[p];
        if ((l & 0xC0) == 0xC0) {                         /* compression pointer */
            if (p + 1 >= len || ++jumps > 8) return -1;
            if (!jumped) *pos = p + 2;
            jumped = true; p = ((l & 0x3F) << 8) | m[p + 1]; continue;
        }
        if (l == 0) { if (!jumped) *pos = p + 1; return ok && label == 2; }
        if (l > 63 || p + 1 + l > len) return -1;
        if (label >= 2 || !label_eq(m + p + 1, LABELS[label], l)) ok = false;
        label++; p += 1 + l;
    }
    return -1;
}

static int put16(uint8_t *o, int k, uint16_t v) { o[k] = (uint8_t)(v >> 8); o[k + 1] = (uint8_t)v; return k + 2; }
static int put32(uint8_t *o, int k, uint32_t v) { k = put16(o, k, (uint16_t)(v >> 16)); return put16(o, k, (uint16_t)v); }
/* our name: written in full the first time, afterwards as a pointer to it */
static int put_name(uint8_t *o, int k, int *name_at) {
    if (*name_at >= 0) return put16(o, k, (uint16_t)(0xC000 | *name_at));
    *name_at = k;
    for (int i = 0; i < 2; i++) { int l = (int)strlen(LABELS[i]); o[k++] = (uint8_t)l; memcpy(o + k, LABELS[i], (size_t)l); k += l; }
    o[k++] = 0;
    return k;
}

int mdnsl_answer(const uint8_t *q, int qlen, const uint8_t ip[4], bool legacy, uint8_t *out, int max) {
    if (qlen < 12 || max < 128 || (q[2] & 0xF8)) return 0;          /* a response or not a standard query */
    int qd = (q[4] << 8) | q[5], pos = 12, qtype = 0;
    bool want_a = false, want_nsec = false;
    for (int i = 0; i < qd; i++) {
        int ours = name_is_ours(q, qlen, &pos);
        if (ours < 0 || pos + 4 > qlen) return 0;
        int type = (q[pos] << 8) | q[pos + 1];
        pos += 4;
        if (!ours) continue;
        if (!qtype) qtype = type;
        if (type == TYPE_A || type == TYPE_ANY) want_a = true;
        if (type != TYPE_A) want_nsec = true;                           /* AAAA etc.: say they do not exist */
    }
    if (!want_a && !want_nsec) return 0;
    /* legacy one-shot query (not from port 5353): echo id + question, short TTL, no cache-flush bit */
    uint16_t cls = legacy ? 0x0001 : 0x8001;
    uint32_t ttl = legacy ? 10 : 120;
    int name_at = -1, k = 0;
    k = put16(out, k, legacy ? (uint16_t)((q[0] << 8) | q[1]) : 0);
    k = put16(out, k, 0x8400);                                          /* response, authoritative */
    k = put16(out, k, legacy ? 1 : 0);
    k = put16(out, k, (uint16_t)(want_a + (want_nsec ? 1 : 0)));        /* answers */
    k = put16(out, k, 0);
    k = put16(out, k, (uint16_t)(want_a && !want_nsec ? 1 : 0));        /* additional: NSEC next to the A record */
    if (legacy) { k = put_name(out, k, &name_at); k = put16(out, k, (uint16_t)qtype); k = put16(out, k, 1); }
    if (want_a) {
        k = put_name(out, k, &name_at); k = put16(out, k, TYPE_A); k = put16(out, k, cls); k = put32(out, k, ttl);
        k = put16(out, k, 4); memcpy(out + k, ip, 4); k += 4;
    }
    /* NSEC: next name = our name, type bitmap window 0 with only bit 1 (A) set */
    k = put_name(out, k, &name_at); k = put16(out, k, TYPE_NSEC); k = put16(out, k, cls); k = put32(out, k, ttl);
    k = put16(out, k, 5); k = put_name(out, k, &name_at); out[k++] = 0; out[k++] = 1; out[k++] = 0x40;
    return k;
}

bool mdnsl_conflict(const uint8_t *m, int len, const uint8_t ip[4]) {
    if (len < 12 || !(m[2] & 0x80)) return false;                      /* only responses */
    int qd = (m[4] << 8) | m[5], rr = ((m[6] << 8) | m[7]) + ((m[8] << 8) | m[9]) + ((m[10] << 8) | m[11]), pos = 12;
    for (int i = 0; i < qd; i++) { if (name_is_ours(m, len, &pos) < 0 || pos + 4 > len) return false; pos += 4; }
    for (int i = 0; i < rr; i++) {
        int ours = name_is_ours(m, len, &pos);
        if (ours < 0 || pos + 10 > len) return false;
        int type = (m[pos] << 8) | m[pos + 1], rdlen = (m[pos + 8] << 8) | m[pos + 9];
        pos += 10;
        if (pos + rdlen > len) return false;
        if (ours && type == TYPE_A && rdlen == 4 && memcmp(m + pos, ip, 4)) return true;   /* someone else has it */
        pos += rdlen;
    }
    return false;
}

/* ------------------------------------------------------------ network glue (lwIP context) */
static struct udp_pcb *m_pcb;
static struct netif *m_netif;
enum { M_OFF, M_PROBE, M_ANNOUNCE, M_READY };
static int m_state = M_OFF, m_step = 0, m_suffix = 1;
static volatile bool m_conflict = false;
static absolute_time_t m_next;
bool mdnsl_ready(void) { return m_state == M_READY; }
static void my_ip(uint8_t ip[4]) {
    const ip4_addr_t *me = netif_ip4_addr(m_netif);
    ip[0] = ip4_addr1(me); ip[1] = ip4_addr2(me); ip[2] = ip4_addr3(me); ip[3] = ip4_addr4(me);
}

static void m_send(const uint8_t *d, int n, const ip_addr_t *to, u16_t port) {
    struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, (u16_t)n, PBUF_RAM);
    if (!p) return;
    memcpy(p->payload, d, (size_t)n);
    udp_sendto(m_pcb, p, to, port);
    pbuf_free(p);
}
static void m_recv(void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *addr, u16_t port) {
    (void)arg; (void)pcb;
    static uint8_t q[512], r[160];
    int n = pbuf_copy_partial(p, q, sizeof q, 0);
    pbuf_free(p);
    uint8_t ip[4]; my_ip(ip);
    if (m_state == M_PROBE || m_state == M_ANNOUNCE) {   /* someone answers for the name we want: take the next one */
        if (mdnsl_conflict(q, n, ip)) m_conflict = true;
        if (m_state == M_PROBE) return;                  /* not ours yet: do not answer */
    }
    bool legacy = port != MDNS_PORT;
    int k = mdnsl_answer(q, n, ip, legacy, r, sizeof r);
    if (!k) return;
    if (legacy) m_send(r, k, addr, port);
    else { ip_addr_t grp; IP4_ADDR(ip_2_ip4(&grp), 224, 0, 0, 251); m_send(r, k, &grp, MDNS_PORT); }
}

static void send_group(const uint8_t *d, int n) { ip_addr_t to; IP4_ADDR(ip_2_ip4(&to), 224, 0, 0, 251); m_send(d, n, &to, MDNS_PORT); }
static int ask_for_name(uint8_t *q) {                   /* "A <name>.local?" */
    int k = 0;
    memset(q, 0, 12); q[5] = 1; k = 12;
    for (int i = 0; i < 2; i++) { int l = (int)strlen(LABELS[i]); q[k++] = (uint8_t)l; memcpy(q + k, LABELS[i], (size_t)l); k += l; }
    q[k++] = 0; q[k++] = 0; q[k++] = 1; q[k++] = 0; q[k++] = 1;
    return k;
}

void mdnsl_start(struct netif *n) {
    if (m_pcb) return;
    m_netif = n;
    ip4_addr_t grp; IP4_ADDR(&grp, 224, 0, 0, 251);
    igmp_joingroup_netif(n, &grp);
    m_pcb = udp_new_ip_type(IPADDR_TYPE_V4);
    if (!m_pcb || udp_bind(m_pcb, IP_ANY_TYPE, MDNS_PORT) != ERR_OK) { m_state = M_READY; return; }
    udp_recv(m_pcb, m_recv, NULL);
    m_state = M_PROBE; m_step = 0; m_conflict = false; m_next = get_absolute_time();
}

void mdnsl_task(void) {
    if (m_state == M_OFF || m_state == M_READY || !time_reached(m_next)) return;
    static uint8_t buf[160];
    if (m_conflict) {                                    /* taken: next suffix, probe again */
        m_conflict = false;
        if (m_suffix < 9) { m_suffix++; snprintf(m_name, sizeof m_name, "%s-%d", m_base, m_suffix); }
        else { m_state = M_READY; return; }              /* give up searching, keep the last one */
        m_state = M_PROBE; m_step = 0; m_next = get_absolute_time();
        return;
    }
    if (m_state == M_PROBE) {
        if (m_step < 3) {
            send_group(buf, ask_for_name(buf));
            m_step++; m_next = make_timeout_time_ms(250);
            return;
        }
        m_state = M_ANNOUNCE; m_step = 0;
    }
    /* announce twice, 1 s apart: caches in the network learn the address right away */
    uint8_t q[64], ip[4]; my_ip(ip);
    int k = mdnsl_answer(q, ask_for_name(q), ip, false, buf, sizeof buf);
    if (k) send_group(buf, k);
    if (++m_step >= 2) m_state = M_READY;
    else m_next = make_timeout_time_ms(1000);
}
