/*
 * Whole playback chain, simulated for 10 minutes per scenario:
 *   Ultimate (own clock) -> WLAN (delay, bursts, losses, reordering) -> jitter buffer -> pcore_tick every 1 ms
 *   -> DAC ring -> DAC running at the rate the dithered PIO divider gives on a Pico whose crystal is off by some ppm.
 * Checked: reserve settles at the target, the learnt clock difference matches, the DAC never runs dry,
 * concealment only where packets really were missing, pitch wobble stays tiny.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "pcore.h"

static int fails = 0;
static void check(int ok, const char *what) { printf("%s %s\n", ok ? "ok  " : "FAIL", what); if (!ok) fails++; }

static double urand(void) { return rand() / (double)RAND_MAX; }

typedef struct {
    const char *name;
    double u64_hz;        /* the Ultimate's real rate */
    double pico_ppm;      /* the Pico's crystal error */
    int    target_ms;
    double stall_max_ms;  /* WLAN stalls every 2..4 s, up to this long */
    double loss;          /* packet loss probability */
    double reorder;       /* probability that a packet is overtaken by the next */
    int    outage_at;     /* s: 150 ms where packets are lost (0 = none) */
    int    stall_at;      /* s: 150 ms where packets are held back and then arrive (0 = none) */
} scen_t;

#define PKTS_MAX 200000
static double arrive[PKTS_MAX];          /* arrival time of every packet, <0 = lost */

static pcore_t P;
static uint8_t pkt[U64_PKT_BYTES];

static void scenario(scen_t sc) {
    const double SECONDS = 600, SYS = 150e6;
    srand(12345);
    /* ---- the network: when does packet k arrive? */
    int npk = (int)(SECONDS * sc.u64_hz / U64_PKT_FRAMES);
    double next_stall = 2.0 + 2.0 * urand(), lost = 0, lost_gap = 0;
    double stall_from = -1, stall_to = -1;
    for (int k = 0; k < npk; k++) {
        double sent = (k + 1) * U64_PKT_FRAMES / sc.u64_hz;            /* the packet is complete at this moment */
        if (sent >= next_stall) { stall_from = sent; stall_to = sent + sc.stall_max_ms / 1000.0 * urand(); next_stall += 2.0 + 2.0 * urand(); }
        double t = sent + 0.0015 + 0.004 * urand() * urand();             /* 1.5 ms + mostly small extra delay */
        if (sent >= stall_from && sent < stall_to) t = stall_to + 0.0001 * (k % 10);
        if (sc.stall_at && sent >= sc.stall_at && sent < sc.stall_at + 0.15) t = sc.stall_at + 0.15 + 0.0001 * (k % 10);
        if (urand() < sc.reorder) t += 0.006;                             /* overtaken by the next packet */
        if (sc.outage_at && sent >= sc.outage_at && sent < sc.outage_at + 0.15) { t = -1; lost_gap++; }
        else if (urand() < sc.loss) { t = -1; lost++; }
        arrive[k] = t;
    }
    /* deliver in time order: simple insertion into a sorted index */
    static int order[PKTS_MAX]; int n = 0;
    for (int k = 0; k < npk; k++) if (arrive[k] >= 0) order[n++] = k;
    for (int i = 1; i < n; i++) { int x = order[i], j = i - 1; while (j >= 0 && arrive[order[j]] > arrive[x]) { order[j + 1] = order[j]; j--; } order[j + 1] = x; }

    /* ---- the Pico */
    pcore_init(&P, 150e6, 47983.0, sc.target_ms * 48);
    pcore_set_volume(&P, 100);
    jit_start(&P.jit);
    static int32_t out[2 * 4096];
    double dac_pos = 0;                                                   /* frames the DAC has played (fractional) */
    long written = PCORE_AHEAD, dac_dry = 0;                           /* the DMA ring starts with silence */
    uint32_t div = P.div_q8;
    int next = 0;
    double sum_fill = 0, sum_ppm = 0, sum_ppm2 = 0; long nsum = 0;
    double fill_min = 1e9, fill_max = 0;
    const double sys_real = SYS * (1.0 + sc.pico_ppm * 1e-6);
    for (long ms = 0; ms < (long)(SECONDS * 1000); ms++) {
        double now = ms / 1000.0;
        /* the DAC played 1 ms at the rate of the current divider */
        double rate = sys_real / (div / 256.0 * PCORE_CYCLES_PER_FRAME);
        dac_pos += rate * 0.001;
        if ((long)dac_pos > written) { dac_dry++; written = (long)dac_pos; }
        /* packets that arrived meanwhile */
        while (next < n && arrive[order[next]] <= now) {
            int k = order[next++];
            jit_push(&P.jit, (uint16_t)(30000 + k), pkt);
        }
        int queued = (int)(written - (long)dac_pos);
        written += pcore_tick(&P, queued, PCORE_AHEAD, out, 4096, 0.001f, &div);
        if (now >= 120 && jit_playing(&P.jit) && !(sc.outage_at && fabs(now - sc.outage_at - 1) < 6) && !(sc.stall_at && fabs(now - sc.stall_at - 1) < 6)) {
            double f = P.clk.fill_avg / 48.0; sum_fill += f; nsum++; sum_ppm += P.clk.ppm; sum_ppm2 += P.clk.ppm * P.clk.ppm;
            if (f < fill_min) fill_min = f;
            if (f > fill_max) fill_max = f;
        }
    }
    double mean = sum_fill / nsum, ppm = sum_ppm / nsum, ppm_sd = sqrt(sum_ppm2 / nsum - ppm * ppm);
    double want_ppm = (sc.u64_hz / (47983.0 * (1.0 + sc.pico_ppm * 1e-6)) - 1.0) * 1e6;
    jit_stats_t *st = &P.jit.st;
    printf("\n== %s\n", sc.name);
    printf("   reserve after 2 min (as steered: max per 0.5 s, smoothed): mean %.1f ms (target %d + 4 ms DAC queue), min %.1f, max %.1f\n", mean, sc.target_ms, fill_min, fill_max);
    printf("   clock correction %.0f ppm (needed %.0f), wobble %.1f ppm sd\n", ppm, want_ppm, ppm_sd);
    printf("   packets lost in WLAN %.0f (+%.0f in the outage) -> missing %u, concealed %u, silent %u, underruns %u, late %u, skipped %u, prefills %u, DAC dry %ld\n",
           lost, lost_gap, st->missing, st->concealed, st->silent, st->underruns, st->late, st->skipped, st->prefills, dac_dry);
    check(fabs(mean - sc.target_ms - PCORE_AHEAD / 48.0) < 2, "reserve settles at the target (+ the 4 ms DAC queue, +-2 ms)");
    check(fabs(ppm - want_ppm) < 30, "clock difference learnt (+-30 ppm)");
    check(ppm_sd < 60, "pitch wobble small (< 60 ppm sd = 0.1 cent)");
    check(dac_dry == 0, "DAC never ran dry");
    int events = (sc.outage_at ? 1 : 0) + (sc.stall_at ? 1 : 0);
    check(st->underruns <= 4u * events && st->prefills <= 1u + events, "runs empty only at the forced 150 ms gaps");
    check(st->missing >= (uint32_t)lost - 1 && st->missing <= (uint32_t)lost + 2 && st->concealed <= st->missing + st->underruns,
          "concealment only where packets were really missing");
    check(st->skipped <= 40u * events, "no packets dropped in normal operation (only when catching up after a gap)");
}

int main(void) {
    for (int i = 0; i < U64_PKT_FRAMES; i++) { int16_t v = (int16_t)(1000 * sin(i * 0.1)); pkt[4 * i] = (uint8_t)v; pkt[4 * i + 1] = (uint8_t)(v >> 8); pkt[4 * i + 2] = pkt[4 * i]; pkt[4 * i + 3] = pkt[4 * i + 1]; }
    scenario((scen_t){ "Elite II as measured (47982.2 Hz), exact Pico, stalls up to 60 ms, 0.2 % loss", 47982.2, 0, 80, 60, 0.002, 0.003, 0, 0 });
    scenario((scen_t){ "fast Ultimate (47990 Hz), Pico crystal -30 ppm, 150 ms outage + 150 ms stall", 47990.0, -30, 80, 60, 0.002, 0.003, 300, 420 });
    scenario((scen_t){ "slow Ultimate (47970 Hz), Pico +25 ppm, target 60 ms, stalls up to 45 ms", 47970.0, 25, 60, 45, 0.001, 0.003, 0, 0 });
    scenario((scen_t){ "target 150 ms, stalls up to 120 ms, 1 % loss", 47982.2, 10, 150, 120, 0.01, 0.01, 0, 0 });
    printf(fails ? "\n%d FAILED\n" : "\nall ok\n", fails);
    return fails != 0;
}
