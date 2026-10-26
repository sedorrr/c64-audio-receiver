/* Playback core - see pcore.h */
#include "pcore.h"

#define GAIN_STEP 64                        /* Q16 per frame: full range in ~21 ms */

void pcore_init(pcore_t *p, double sys_hz, double nominal_hz, int target_frames) {
    jit_init(&p->jit, target_frames);
    clk_init(&p->clk, nominal_hz);
    p->sys_hz = sys_hz;
    p->gain = p->gain_want = 0;
    p->was_playing = false;
    p->div_q8 = clk_divider_q8(&p->clk, sys_hz, clk_rate(&p->clk), PCORE_CYCLES_PER_FRAME);
    p->fill_ms = 0;
}

int32_t pcore_volume_gain(int pct) {
    if (pct <= 0) return 0;
    if (pct >= 100) return 65536;
    return (int32_t)((int64_t)pct * pct * 65536 / 10000);
}
void pcore_set_volume(pcore_t *p, int pct) { p->gain_want = pcore_volume_gain(pct); }

int pcore_tick(pcore_t *p, int queued, int ahead, int32_t *out, int max, float dt_s, uint32_t *div_q8) {
    int n = ahead - queued;
    if (n > max) n = max;
    if (n < 0) n = 0;
    for (int done = 0; done < n; ) {                 /* in chunks through a small buffer */
        int16_t s[2 * 64];
        int k = n - done < 64 ? n - done : 64;
        jit_pull(&p->jit, s, k);
        int32_t *o = out + 2 * done;
        for (int i = 0; i < k; i++) {
            int32_t g = p->gain, d = p->gain_want - g;   /* gain change spread over the frames (no zipper noise) */
            p->gain = g + (d > GAIN_STEP ? GAIN_STEP : (d < -GAIN_STEP ? -GAIN_STEP : d));
            o[2 * i]     = (int32_t)s[2 * i] * g;        /* 16 bit -> 32 bit MSB-aligned, times the volume */
            o[2 * i + 1] = (int32_t)s[2 * i + 1] * g;
        }
        done += k;
    }
    /* the reserve is what waits in the buffer plus what is already queued for the DAC */
    int fill = jit_fill(&p->jit) + queued + n;
    p->fill_ms = (float)fill / 48.0f;
    bool playing = jit_playing(&p->jit);
    if (playing && !p->was_playing) clk_restart(&p->clk);
    p->was_playing = playing;
    if (playing) clk_update(&p->clk, (float)fill, (float)(p->jit.target + PCORE_AHEAD), dt_s);   /* the DAC queue comes on top */
    p->div_q8 = clk_divider_q8(&p->clk, p->sys_hz, clk_rate(&p->clk), PCORE_CYCLES_PER_FRAME);
    *div_q8 = p->div_q8;
    return n;
}
