/*
 * Settings that change during use (volume, source, playing on/off): one 16-byte record per save in the last
 * flash sector. Records are appended (a flash page can be programmed again where it is still erased), so the
 * sector is erased only every 256 saves - most saves take ~1 ms instead of a 45 ms erase.
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint8_t volume;                 /* percent */
    uint8_t src;                    /* selected Ultimate (0..3) */
    uint8_t play;                   /* playing when switched on */
} prefs_t;

void prefs_load(prefs_t *p);         /* the newest valid record, or the defaults */
void prefs_save(const prefs_t *p);   /* main loop only (writes flash) */
/* backup: the current values as one record in the flash format (it carries its own check) */
#define PREFS_PACKED 16
void prefs_pack(const prefs_t *p, uint8_t out[PREFS_PACKED]);
bool prefs_unpack(const uint8_t *d, int n, prefs_t *p);   /* false = not a valid record */
