/*
 * Playback core: jitter buffer + clock control + volume, one "tick" at a time. Portable C (host-tested).
 * The platform part (i2s_out.c) calls pcore_tick() every millisecond from a timer interrupt with the number
 * of frames still queued for the DAC; it gets frames to queue and the next PIO divider back.
 * Everything in here runs in that one context - packets reach it through a queue (player.c).
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "jitter.h"
#include "clockctl.h"

#define PCORE_AHEAD             (48 * 4)    /* frames kept queued for the DAC between ticks (4 ms) */
#define PCORE_CYCLES_PER_FRAME  128         /* PIO cycles per stereo frame: 2 x 32 bits, 2 instructions per bit */

typedef struct {
    jit_t    jit;
    clk_t    clk;
    double   sys_hz;                        /* PIO input clock */
    int32_t  gain, gain_want;               /* volume, Q16 (65536 = full scale) */
    bool     was_playing;
    uint32_t div_q8;                        /* last PIO divider (16.8) */
    float    fill_ms;                       /* last measured reserve (buffer + DAC queue) */
} pcore_t;

void pcore_init(pcore_t *p, double sys_hz, double nominal_hz, int target_frames);
void pcore_set_volume(pcore_t *p, int pct);         /* 0..100 %, square law (50 % = -12 dB); ramped, no zipper noise */
int32_t pcore_volume_gain(int pct);
/* one tick: `queued` = frames still waiting in the DAC ring, `ahead` = how many should wait afterwards
   (PCORE_AHEAD; more before a flash write). Writes up to `max` frames (L, R as 32-bit words, MSB-aligned
   for the PCM5102A) into out, returns how many; *div_q8 = PIO divider (16.8) for the next tick */
int  pcore_tick(pcore_t *p, int queued, int ahead, int32_t *out, int max, float dt_s, uint32_t *div_q8);
