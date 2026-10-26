/* "Now playing" relay to the visualizer - see np_relay.h */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "pico/rand.h"
#include "lwip/tcp.h"
#include "md5.h"
#include "diag.h"
#include "np_relay.h"

#define PATH_MAX_LEN 400
#define USER         "c64audio"                  /* the visualizer does not check the user name */
#define RETRY_MS     300                         /* visualizer busy (409) or not reachable: try again */
#define GIVE_UP_MS   15000                       /* a message older than this is dropped (the PC repeats every 30 s) */

static char q_path[NPR_QUEUE][PATH_MAX_LEN];
static absolute_time_t q_born[NPR_QUEUE];
static volatile uint32_t q_w, q_r;

static struct tcp_pcb *c_pcb;
static volatile bool c_busy;                     /* a request is in flight */
static volatile int  c_code;                     /* HTTP status of the finished request, -1 = no answer */
static volatile bool c_done;
static char c_req[PATH_MAX_LEN + 600], c_rx[700];
static int  c_rxlen;
static absolute_time_t c_t0, c_next;
static char nonce[64], realm[48];
static uint32_t nc;
static bool sent_auth;                           /* the request in flight carried a Digest answer */
static int  tries;
static char state[48] = "";
static uint32_t n_sent;

const char *npr_state(void) { return state; }
uint32_t npr_sent(void) { return n_sent; }

void npr_queue(const char *path) {
    uint32_t w = q_w;
    if (w - q_r >= NPR_QUEUE) { DIAG("titel: Warteschlange voll - Meldung verworfen"); return; }
    snprintf(q_path[w % NPR_QUEUE], PATH_MAX_LEN, "%s", path);
    q_born[w % NPR_QUEUE] = get_absolute_time();
    q_w = w + 1;
}

/* value of key in a WWW-Authenticate header (quoted or not) */
static bool hparam(const char *h, const char *key, char *out, int n) {
    size_t kl = strlen(key);
    for (const char *p = h; (p = strstr(p, key)) != NULL; p += kl) {
        if (p != h && p[-1] != ' ' && p[-1] != ',') continue;
        const char *v = p + kl;
        if (*v != '=') continue;
        v++;
        bool q = *v == '"';
        if (q) v++;
        int k = 0;
        while (*v && k < n - 1 && (q ? *v != '"' : (*v != ',' && *v != '\r'))) out[k++] = *v++;
        out[k] = 0;
        return true;
    }
    return false;
}

/* ---- the connection (lwIP context) */
static bool close_pcb(struct tcp_pcb *p) {
    tcp_arg(p, NULL); tcp_recv(p, NULL); tcp_err(p, NULL); tcp_poll(p, NULL, 0);
    c_pcb = NULL;
    if (tcp_close(p) == ERR_OK) return false;
    tcp_abort(p);
    return true;
}
static void finish(int code) { c_code = code; c_done = true; c_busy = false; }
static err_t on_recv(void *arg, struct tcp_pcb *p, struct pbuf *b, err_t err) {
    (void)arg; (void)err;
    if (!b) {
        c_rx[c_rxlen] = 0;
        finish(strncmp(c_rx, "HTTP/1.", 7) ? -1 : atoi(c_rx + 9));
        return close_pcb(p) ? ERR_ABRT : ERR_OK;
    }
    int room = (int)sizeof c_rx - 1 - c_rxlen;
    if (room > 0) c_rxlen += pbuf_copy_partial(b, c_rx + c_rxlen, (u16_t)(b->tot_len < room ? b->tot_len : room), 0);
    tcp_recved(p, b->tot_len);
    pbuf_free(b);
    return ERR_OK;
}
static void on_err(void *arg, err_t err) { (void)arg; (void)err; c_pcb = NULL; finish(-1); }
static err_t on_poll(void *arg, struct tcp_pcb *p) {
    (void)arg;
    if (absolute_time_diff_us(c_t0, get_absolute_time()) < 5000000) return ERR_OK;
    tcp_arg(p, NULL); tcp_recv(p, NULL); tcp_err(p, NULL); tcp_poll(p, NULL, 0);
    tcp_abort(p);
    c_pcb = NULL; finish(-1);
    return ERR_ABRT;
}
static err_t on_connected(void *arg, struct tcp_pcb *p, err_t err) {
    (void)arg;
    if (err != ERR_OK) { bool ab = close_pcb(p); finish(-1); return ab ? ERR_ABRT : ERR_OK; }
    tcp_write(p, c_req, (u16_t)strlen(c_req), TCP_WRITE_FLAG_COPY);
    tcp_output(p);
    return ERR_OK;
}

/* ---- main loop */
static void send_one(uint32_t ip, const char *host, const char *pw, const char *path) {
    char auth[700] = "";
    sent_auth = nonce[0] != 0;
    if (sent_auth) {
        char buf[600], ha1[33], ha2[33], resp[33], cnonce[17], ncs[9];
        snprintf(cnonce, sizeof cnonce, "%08lx%08lx", (unsigned long)get_rand_32(), (unsigned long)get_rand_32());
        snprintf(ncs, sizeof ncs, "%08lx", (unsigned long)++nc);
        snprintf(buf, sizeof buf, USER ":%s:%s", realm, pw);  md5_hex(buf, strlen(buf), ha1);
        snprintf(buf, sizeof buf, "POST:%s", path);            md5_hex(buf, strlen(buf), ha2);
        snprintf(buf, sizeof buf, "%s:%s:%s:%s:auth:%s", ha1, nonce, ncs, cnonce, ha2); md5_hex(buf, strlen(buf), resp);
        snprintf(auth, sizeof auth, "Authorization: Digest username=\"" USER "\", realm=\"%s\", nonce=\"%s\", uri=\"%s\", "
                 "algorithm=MD5, response=\"%s\", qop=auth, nc=%s, cnonce=\"%s\"\r\n", realm, nonce, path, resp, ncs, cnonce);
    }
    snprintf(c_req, sizeof c_req, "POST %s HTTP/1.1\r\nHost: %s\r\n%sContent-Length: 0\r\nConnection: close\r\n\r\n", path, host, auth);
    c_rxlen = 0; c_done = false; c_busy = true; c_t0 = get_absolute_time();
    ip_addr_t a; ip4_addr_set_u32(ip_2_ip4(&a), ip);
    c_pcb = tcp_new_ip_type(IPADDR_TYPE_V4);
    if (!c_pcb) { finish(-1); return; }
    tcp_recv(c_pcb, on_recv); tcp_err(c_pcb, on_err); tcp_poll(c_pcb, on_poll, 2);
    if (tcp_connect(c_pcb, &a, 80, on_connected) != ERR_OK) { tcp_abort(c_pcb); c_pcb = NULL; finish(-1); }
}

void npr_task(uint32_t ip, const char *host, const char *pw) {
    if (c_busy) return;
    if (c_done) {                                         /* the answer to the request just sent */
        c_done = false;
        if (c_code == 200) {                              /* taken: the next one right away */
            n_sent++; q_r++; tries = 0; strcpy(state, "übergeben");
            c_next = get_absolute_time();
            return;
        } else if (c_code == 401) {
            char st[8] = "";
            const char *h = strstr(c_rx, "WWW-Authenticate:");
            bool stale = h && hparam(h, "stale", st, sizeof st) && !strcmp(st, "true");
            if (h) { hparam(h, "nonce", nonce, sizeof nonce); hparam(h, "realm", realm, sizeof realm); nc = 0; }
            if (sent_auth && !stale) {                    /* our answer was wrong: the password */
                DIAG("titel: Visualizer lehnt das Passwort ab - Meldung verworfen");
                strcpy(state, "Passwort falsch"); q_r++; tries = 0; nonce[0] = 0;
            } else if (++tries > 3) { q_r++; tries = 0; strcpy(state, "Anmeldung klappt nicht"); }
            else { c_next = get_absolute_time(); return; } /* answer the challenge right away */
        } else if (c_code == 409) {
            strcpy(state, "Visualizer beschäftigt");      /* previous message not taken yet */
        } else {
            snprintf(state, sizeof state, c_code < 0 ? "nicht erreichbar" : "Fehler %d", c_code);
            if (++tries > 10) { q_r++; tries = 0; }
        }
        c_next = make_timeout_time_ms(RETRY_MS);
        return;
    }
    while (q_r != q_w && absolute_time_diff_us(q_born[q_r % NPR_QUEUE], get_absolute_time()) > GIVE_UP_MS * 1000ll) {
        q_r++; tries = 0; DIAG("titel: Meldung zu alt - verworfen");
    }
    if (q_r == q_w || !ip || !time_reached(c_next)) return;
    send_one(ip, host, pw, q_path[q_r % NPR_QUEUE]);
}
