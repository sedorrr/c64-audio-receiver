/*
 * Firmware update over the web page (Pico 2 W)
 *  - the browser reads the .uf2, keeps the RP2350 blocks and sends the flat image in chunks:
 *      POST /fw/begin?size=N   POST /fw/chunk?off=X (body)   POST /fw/end?hash=H   POST /fw/apply
 *    GET /fw/state shows the progress
 *  - the image is staged in free flash between 2 MB and the WLAN settings, checked (FNV-1a hash
 *    from the browser, IMAGE_DEF block in the first sector, FW_MARKER = firmware of this audio receiver,
 *    not e.g. the visualizer's) and only then copied over the running firmware (ota_apply.c)
 *  - playback stops for the update: every flash erase stops all interrupts for ~45 ms
 * Like everywhere else: the web routes only take data and set flags, flash writes happen in ota_task().
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "hardware/watchdog.h"
#include "hardware/regs/addressmap.h"
#include "config.h"
#include "net_internal.h"
#include "player.h"
#include "i2s_out.h"
#include "diag.h"
#include "ota.h"

#define STAGE_OFFSET OTA_STAGE_OFFSET                                         /* 0x200000 */
#define STAGE_END    NETCFG_OFFSET                                            /* WLAN settings start here */
#define STAGE_SIZE   (STAGE_END - STAGE_OFFSET)                              /* ~2 MB */
#define CHUNK_MAX    32768
#define IMAGE_MARKER 0xffffded3u                                             /* PICOBIN_BLOCK_MARKER_START */

typedef enum { O_IDLE, O_RECV, O_CHECK, O_READY, O_APPLY, O_ERR } ostate_t;
static volatile ostate_t o_st = O_IDLE;
static uint32_t o_size, o_got, o_hash;
static volatile int o_chunk_len = -1;                    /* chunk waiting in the shared body buffer */
static const uint8_t *o_chunk;
static uint32_t o_chunk_off;
static volatile bool o_check_req = false;
static absolute_time_t o_apply_at;
static char o_msg[64] = "";

const char *ota_build(void) { return __DATE__ " " __TIME__; }
bool ota_busy_body(void) { return o_chunk_len >= 0; }
bool ota_active(void) { return o_st == O_RECV || o_st == O_CHECK || o_st == O_READY || o_st == O_APPLY; }

static uint32_t fnv_flash(uint32_t off, uint32_t n) {
    const uint8_t *p = (const uint8_t *)(XIP_BASE + off);
    uint32_t h = 2166136261u;
    for (uint32_t i = 0; i < n; i++) {
        h ^= p[i]; h *= 16777619u;
        if ((i & 0xFFFF) == 0) watchdog_update();
    }
    return h;
}
static bool flash_has(uint32_t off, uint32_t n, const char *needle) {
    const uint8_t *p = (const uint8_t *)(XIP_BASE + off);
    size_t k = strlen(needle);
    for (uint32_t i = 0; i + k <= n; i++) {
        if (p[i] == (uint8_t)needle[0] && !memcmp(p + i, needle, k)) return true;
        if ((i & 0xFFFF) == 0) watchdog_update();
    }
    return false;
}
static void fail(const char *m) { o_st = O_ERR; snprintf(o_msg, sizeof o_msg, "%s", m); DIAG("ota: %s", m); }

/* main loop: write a received chunk into the staging area (erase its sectors first) */
static void write_chunk(void) {
    static uint8_t page[FLASH_PAGE_SIZE];
    uint32_t off = STAGE_OFFSET + o_chunk_off, n = (uint32_t)o_chunk_len;
    for (uint32_t s = 0; s < n; s += FLASH_SECTOR_SIZE) {
        watchdog_update();
        uint32_t irq = save_and_disable_interrupts();
        flash_range_erase(off + s, FLASH_SECTOR_SIZE);
        restore_interrupts(irq);
    }
    for (uint32_t p = 0; p < n; p += FLASH_PAGE_SIZE) {
        const uint8_t *src = o_chunk + p;
        if (n - p < FLASH_PAGE_SIZE) {                     /* last page of the image: pad with erased bytes */
            memset(page, 0xFF, sizeof page); memcpy(page, src, n - p); src = page;
        }
        uint32_t irq = save_and_disable_interrupts();
        flash_range_program(off + p, src, FLASH_PAGE_SIZE);
        restore_interrupts(irq);
        if ((p & 0xFFF) == 0) watchdog_update();
    }
    o_got = o_chunk_off + n;
    o_chunk_len = -1;
}

void ota_task(void) {
    if (o_chunk_len >= 0) write_chunk();
    if (o_check_req) {
        o_check_req = false;
        o_st = O_CHECK; strcpy(o_msg, "prüfe ...");
        uint32_t h = fnv_flash(STAGE_OFFSET, o_size);
        const uint32_t *w = (const uint32_t *)(XIP_BASE + STAGE_OFFSET);
        bool marker = false;
        for (int i = 0; i < FLASH_SECTOR_SIZE / 4 && !marker; i++) marker = w[i] == IMAGE_MARKER;
        if (h != o_hash) fail("Prüfsumme falsch - Übertragung fehlerhaft");
        else if (!marker) fail("keine gültige RP2350-Firmware");
        else if (!flash_has(STAGE_OFFSET, o_size, FW_MARKER)) fail("keine Firmware des C64-Audioempfängers");
        else { o_st = O_READY; strcpy(o_msg, "geprüft - bereit"); DIAG("ota: %lu Bytes geprüft", (unsigned long)o_size); }
    }
    if (o_st == O_APPLY && time_reached(o_apply_at)) {
        DIAG("ota: kopiere %lu Bytes und starte neu", (unsigned long)o_size);
        i2s_out_stop();                                  /* interrupts stay off for seconds: the DMA must not loop the ring */
        ota_apply_now(STAGE_OFFSET, o_size, (uint8_t *)net_body_buffer());
    }
}

static int reply(char *out, int n, const char *json, const char **o, int *olen, const char **type, int code) {
    *olen = snprintf(out, (size_t)n, "%s", json); *o = out; *type = "application/json"; return code;
}

int ota_route(bool post, const char *path, const char *body, int len, const char **o, int *olen, const char **type) {
    static char out[256];
    if (strncmp(path, "/fw/", 4)) return 0;
    const char *q = strchr(path, '?');
    if (!post && !strcmp(path, "/fw/state")) {
        static const char *st[] = { "idle", "recv", "check", "ready", "apply", "error" };
        *olen = snprintf(out, sizeof out, "{\"state\":\"%s\",\"got\":%lu,\"size\":%lu,\"msg\":\"%s\",\"max\":%u,\"build\":\"%s\"}",
                         st[o_st], (unsigned long)o_got, (unsigned long)o_size, o_msg, (unsigned)STAGE_SIZE, ota_build());
        *o = out; *type = "application/json"; return 200;
    }
    if (!post) return 0;
    if (!strncmp(path, "/fw/begin", 9)) {
        if (o_st == O_APPLY || o_chunk_len >= 0) return reply(out, sizeof out, "{\"ok\":false}", o, olen, type, 409);
        const char *s = q ? strstr(q, "size=") : NULL;
        uint32_t n = s ? (uint32_t)strtoul(s + 5, NULL, 10) : 0;
        if (n < FLASH_SECTOR_SIZE || n > STAGE_SIZE) { fail("Firmware zu groß oder leer"); return reply(out, sizeof out, "{\"ok\":false}", o, olen, type, 400); }
        o_size = n; o_got = 0; o_hash = 0; o_st = O_RECV;
        snprintf(o_msg, sizeof o_msg, "empfange %lu KB", (unsigned long)(n / 1024));
        DIAG("ota: beginne, %lu Bytes - Wiedergabe angehalten", (unsigned long)n);
        player_play(false);
        return reply(out, sizeof out, "{\"ok\":true}", o, olen, type, 200);
    }
    if (!strncmp(path, "/fw/chunk", 9)) {
        const char *s = q ? strstr(q, "off=") : NULL;
        uint32_t off = s ? (uint32_t)strtoul(s + 4, NULL, 10) : 0xFFFFFFFFu;
        if (o_st != O_RECV || o_chunk_len >= 0 || !body || len <= 0) return reply(out, sizeof out, "{\"ok\":false}", o, olen, type, 409);
        if (len > CHUNK_MAX || off != o_got || off + (uint32_t)len > o_size || (off & (FLASH_SECTOR_SIZE - 1))) {
            fail("Datenblock passt nicht"); return reply(out, sizeof out, "{\"ok\":false}", o, olen, type, 400);
        }
        o_chunk = (const uint8_t *)body; o_chunk_off = off; o_chunk_len = len;
        return reply(out, sizeof out, "{\"ok\":true}", o, olen, type, 200);
    }
    if (!strncmp(path, "/fw/end", 7)) {
        const char *s = q ? strstr(q, "hash=") : NULL;
        if (o_st != O_RECV || o_chunk_len >= 0 || o_got != o_size || !s) return reply(out, sizeof out, "{\"ok\":false}", o, olen, type, 409);
        o_hash = (uint32_t)strtoul(s + 5, NULL, 16);
        o_check_req = true;
        return reply(out, sizeof out, "{\"ok\":true}", o, olen, type, 200);
    }
    if (!strcmp(path, "/fw/apply")) {
        if (o_st != O_READY) return reply(out, sizeof out, "{\"ok\":false}", o, olen, type, 409);
        o_st = O_APPLY; strcpy(o_msg, "spiele auf - Neustart");
        o_apply_at = make_timeout_time_ms(800);            /* let this answer reach the browser first */
        return reply(out, sizeof out, "{\"ok\":true}", o, olen, type, 200);
    }
    return 0;
}
