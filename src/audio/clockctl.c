/* Clock control for the DAC - see clockctl.h */
#include <math.h>
#include "clockctl.h"

static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

void clk_init(clk_t *c, double nominal_hz) {
    c->nominal = nominal_hz; c->fill_avg = 0; c->have_avg = false; c->integ = 0; c->ppm = 0; c->dither = 0;
    c->win_max = 0; c->win_t = 0;
}
void clk_restart(clk_t *c) { c->have_avg = false; c->win_max = 0; c->win_t = 0; }

double clk_rate(const clk_t *c) { return c->nominal * (1.0 + c->ppm * 1e-6); }

double clk_update(clk_t *c, float fill, float target, float dt_s) {
    if (fill > c->win_max) c->win_max = fill;
    c->win_t += dt_s;
    if (!c->have_avg) { c->fill_avg = fill; c->have_avg = true; c->win_max = 0; c->win_t = 0; }
    else if (c->win_t >= CLK_WIN_S) {
        c->fill_avg += (c->win_max - c->fill_avg) * clampf(c->win_t / CLK_TAU_S, 0.0f, 1.0f);
        c->win_max = 0; c->win_t = 0;
    }
    float err = c->fill_avg - target;                       /* > 0: too much waiting -> play faster */
    float p = CLK_KP * err;
    if (err > -CLK_I_BAND && err < CLK_I_BAND)              /* only small errors teach the clock difference */
        c->integ = clampf(c->integ + CLK_KP * err * dt_s / CLK_TI_S, -CLK_PPM_MAX, CLK_PPM_MAX);
    c->ppm = clampf(c->integ + p, -CLK_PPM_MAX, CLK_PPM_MAX);
    return clk_rate(c);
}

uint32_t clk_divider_q8(clk_t *c, double sys_hz, double rate, int cycles_per_frame) {
    double want = sys_hz * 256.0 / (rate * cycles_per_frame) + c->dither;
    double q = floor(want + 0.5);
    c->dither = want - q;
    return (uint32_t)q;
}
