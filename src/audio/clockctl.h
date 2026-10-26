/*
 * Clock control for the DAC. Portable C (host-tested).
 *
 * The Ultimate's audio clock (~47983 Hz) and the Pico's crystal never run exactly alike. Instead of
 * resampling, the I2S clock itself follows the Ultimate: a PI controller steers the output rate so the
 * jitter buffer stays at its target (+-CLK_PPM_MAX around the nominal rate; 1000 ppm = 1.7 cent, inaudible).
 *
 * The PIO clock divider has 8 fractional bits - one step is ~160 ppm at 48 kHz. The wanted divider is
 * therefore dithered (error feedback) on every update: the average rate is exact, the remaining
 * wobble is +-80 ppm at ~1 kHz, far below anything audible, and the DAC's PLL smooths it anyway.
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>

#define CLK_PPM_MAX   1000.0f
#define CLK_KP        2.0f       /* ppm per frame of error: a 10 ms error is worked off in ~10 s */
#define CLK_TI_S      30.0f      /* integral time: the clock difference is learnt within a minute */
#define CLK_TAU_S     2.0f       /* smoothing of the measured reserve */
#define CLK_WIN_S     0.5f       /* the reserve is measured as the maximum of each window (see clk_update) */
#define CLK_I_BAND    960.0f     /* frames (20 ms): larger errors (after a gap) are not integrated - no windup */

typedef struct {
    double nominal;              /* Hz */
    float  fill_avg;             /* smoothed reserve */
    bool   have_avg;
    float  win_max, win_t;       /* maximum of the current window */
    float  integ;                /* ppm: the learnt clock difference */
    float  ppm;                  /* current correction */
    double dither;               /* divider error feedback, 1/256 steps */
} clk_t;

void   clk_init(clk_t *c, double nominal_hz);
void   clk_restart(clk_t *c);    /* playback (re)started: forget the averaged fill, keep the learnt clock difference */
/* one control step: fill and target in frames (buffer + frames queued for the DAC), dt in seconds; returns the rate in Hz.
   WLAN delivers packets late in bursts, which only ever lowers the reserve for a moment. The maximum over
   half a second is the reserve as it is without those delays - steering that keeps the pitch steady. */
double clk_update(clk_t *c, float fill, float target, float dt_s);
double clk_rate(const clk_t *c);
/* PIO divider for `rate` in 1/256 steps (16.8 fixed point), dithered: cycles_per_frame = PIO cycles per stereo frame */
uint32_t clk_divider_q8(clk_t *c, double sys_hz, double rate, int cycles_per_frame);
