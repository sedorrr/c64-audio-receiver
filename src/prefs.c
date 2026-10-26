/* Settings that change during use - see prefs.h */
#include <string.h>
#include <stddef.h>
#include "pico/stdlib.h"
#include "hardware/flash.h"
#include "hardware/regs/addressmap.h"
#include "config.h"
#include "player.h"
#include "prefs.h"

#define MAGIC 0xA0D1F001u
#define RECS  (FLASH_SECTOR_SIZE / sizeof(rec_t))
typedef struct {
    uint32_t magic, seq;
    uint8_t  volume, src, play, pad;
    uint32_t check;
} rec_t;
_Static_assert(sizeof(rec_t) == 16, "16-byte records");

static uint32_t check_of(const rec_t *r) {
    const uint8_t *p = (const uint8_t *)r;
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < offsetof(rec_t, check); i++) { h ^= p[i]; h *= 16777619u; }
    return h;
}
static const rec_t *flash_recs(void) { return (const rec_t *)(XIP_BASE + PREFS_OFFSET); }
static bool valid(const rec_t *r) { return r->magic == MAGIC && r->check == check_of(r); }

/* newest valid record (highest seq), -1 = none; *free_at = first erased slot behind it (RECS = sector full) */
static int newest(unsigned *free_at) {
    const rec_t *r = flash_recs();
    int best = -1;
    unsigned last_used = 0;
    for (unsigned i = 0; i < RECS; i++) {
        bool erased = true;
        for (unsigned k = 0; k < sizeof(rec_t); k++) if (((const uint8_t *)&r[i])[k] != 0xFF) { erased = false; break; }
        if (!erased) last_used = i + 1;
        if (valid(&r[i]) && (best < 0 || (int32_t)(r[i].seq - r[best].seq) > 0)) best = (int)i;
    }
    *free_at = last_used;
    return best;
}

void prefs_pack(const prefs_t *p, uint8_t out[PREFS_PACKED]) {
    rec_t r = { MAGIC, 0, p->volume, p->src, p->play, 0, 0 };
    r.check = check_of(&r);
    memcpy(out, &r, sizeof r);
}
bool prefs_unpack(const uint8_t *d, int n, prefs_t *p) {
    rec_t r;
    if (n != (int)sizeof r) return false;
    memcpy(&r, d, sizeof r);
    if (!valid(&r) || r.volume > 100 || r.src >= 4) return false;
    p->volume = r.volume; p->src = r.src; p->play = r.play ? 1 : 0;
    return true;
}

void prefs_load(prefs_t *p) {
    unsigned f;
    int i = newest(&f);
    p->volume = VOLUME_DEFAULT; p->src = 0; p->play = 1;
    if (i < 0) return;
    const rec_t *r = &flash_recs()[i];
    p->volume = r->volume > 100 ? 100 : r->volume;
    p->src = r->src < 4 ? r->src : 0;
    p->play = r->play ? 1 : 0;
}

void prefs_save(const prefs_t *p) {
    unsigned f;
    int i = newest(&f);
    rec_t r = { MAGIC, i < 0 ? 1 : flash_recs()[i].seq + 1, p->volume, p->src, p->play, 0, 0 };
    r.check = check_of(&r);
    static uint8_t page[FLASH_PAGE_SIZE];
    bool erase = f >= RECS;
    if (erase) f = 0;
    uint32_t page_off = (uint32_t)(f * sizeof(rec_t)) & ~(uint32_t)(FLASH_PAGE_SIZE - 1);
    memset(page, 0xFF, sizeof page);                     /* 0xFF leaves the other records of the page as they are */
    memcpy(page + (f * sizeof(rec_t) - page_off), &r, sizeof r);
    uint32_t irq = player_flash_begin(erase);            /* before an erase: up to 60 ms of audio queued; interrupts off */
    if (erase) flash_range_erase(PREFS_OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(PREFS_OFFSET + page_off, page, FLASH_PAGE_SIZE);
    player_flash_end(irq);
}
