/*
 * Player: connects the network (packets), the playback core (pcore.c) and the I2S output (i2s_out.c).
 * A timer interrupt with the highest priority runs every millisecond: it takes the queued packets into the
 * jitter buffer, tops up the DAC ring and sets the PIO clock. Network and web page only queue packets and
 * set requests - they never touch the jitter buffer themselves.
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "audio/jitter.h"

typedef struct {
    jit_stats_t jit;
    int      state;                 /* JIT_IDLE / JIT_PREFILL / JIT_PLAY */
    float    fill_ms;               /* reserve now (buffer + DAC queue) */
    float    avg_ms;                /* reserve as steered (max per 0.5 s, smoothed) */
    float    ppm;                   /* clock correction against the nominal 47983 Hz */
    double   rate;                  /* output rate in Hz */
    uint32_t qdrop;                 /* packets dropped because the queue to the timer was full */
    uint32_t behind;                /* the writer fell behind the DMA (should stay 0) */
    int      target_ms, volume;
} player_stats_t;

void player_init(int target_ms, int volume);
void player_packet(uint16_t seq, const uint8_t *pcm_le);   /* any context: one audio packet (U64_PKT_FRAMES frames) */
void player_play(bool on);                                   /* start (collect the reserve, play) / stop (fade out) */
void player_restart(void);                                   /* new source: fade out, collect the reserve again */
void player_set_volume(int pct);
void player_stats(player_stats_t *s);
void player_reset_stats(void);
/* flash writes stop all interrupts: a sector erase ~45 ms (erase = true: queue up to 60 ms of audio first, never more
   than is buffered, and silence the rest of the ring), a page program ~1 ms (erase = false: the 4 ms queue suffices) */
uint32_t player_flash_begin(bool erase);
void     player_flash_end(uint32_t irq);
