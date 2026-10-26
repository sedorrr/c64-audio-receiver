/* Jitter buffer: playback, reordering, losses, gaps, restarts, overfill, stop - checked sample by sample */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "jitter.h"

static int fails = 0;
static void check(int ok, const char *what) { printf("%s %s\n", ok ? "ok  " : "FAIL", what); if (!ok) fails++; }

static jit_t J;
#define N U64_PKT_FRAMES

/* test signals: a counter (bit-exact checks) or a 1 kHz sine (continuity checks) */
static int g_sine = 0;
static int16_t sig_l(long f) { return g_sine ? (int16_t)lrint(10000 * sin(2 * M_PI * 1000.0 * f / 48000.0)) : (int16_t)(f & 0x7FFF); }
static int16_t sig_r(long f) { return g_sine ? (int16_t)lrint(8000 * cos(2 * M_PI * 700.0 * f / 48000.0)) : (int16_t)(f >> 15); }
static void make_pkt(uint8_t *p, uint16_t seq, long first_frame) {
    for (int i = 0; i < N; i++) {
        int16_t l = sig_l(first_frame + i), r = sig_r(first_frame + i);
        p[4 * i] = (uint8_t)l; p[4 * i + 1] = (uint8_t)(l >> 8); p[4 * i + 2] = (uint8_t)r; p[4 * i + 3] = (uint8_t)(r >> 8);
    }
    (void)seq;
}
static void push(uint16_t seq, long pkt_index) { uint8_t p[4 * N]; make_pkt(p, seq, pkt_index * N); jit_push(&J, seq, p); }

/* output log */
#define OUTMAX (48000 * 20)
static int16_t OUT[OUTMAX * 2]; static long nout;
static void pull(int frames) { jit_pull(&J, OUT + 2 * nout, frames); nout += frames; }

/* largest step between neighbouring output samples (left and right) in [a, b) */
static int max_step(long a, long b) {
    int m = 0;
    for (long i = a + 1; i < b; i++)
        for (int c = 0; c < 2; c++) { int d = abs(OUT[2 * i + c] - OUT[2 * (i - 1) + c]); if (d > m) m = d; }
    return m;
}
/* first frame that is not silent */
static long first_sound(void) { for (long i = 0; i < nout; i++) if (OUT[2 * i] || OUT[2 * i + 1]) return i; return -1; }

/* run: packets k = 0.. arrive one per 192 output frames; deliver(k) says when (in packets) packet k arrives
   (-1 = never). Returns nothing, fills OUT. */
static void run(uint16_t seq0, int npk, double (*deliver)(int k), int tail_pkts) {
    nout = 0;
    static double due[20000];
    for (int k = 0; k < npk; k++) due[k] = deliver(k);
    for (int t = 0; t < npk + tail_pkts; t++) {
        for (int k = 0; k < npk; k++) if (due[k] >= t && due[k] < t + 1) push((uint16_t)(seq0 + k), k);
        pull(N);
    }
}
static double d_inorder(int k) { return k; }
static double d_swap(int k) { return (k % 2) ? k - 1 : k + 1.5; }                       /* every pair arrives swapped */
static double d_lost(int k) { return k == 100 ? -1 : k; }
static double d_lost3(int k) { return (k >= 100 && k < 103) ? -1 : k; }
static double d_burst(int k) { return (k >= 100 && k < 115) ? 115 : k; }                 /* 60 ms stall, then all at once */
static double d_gap(int k) { return (k >= 100 && k < 150) ? 150 : k; }                   /* 200 ms stall: longer than the reserve */
static double d_late(int k) { return k == 100 ? 130 : k; }                                /* comes long after its time */

int main(void) {
    const int TARGET = 48 * 80;                                                           /* 80 ms */
    /* 1. in order: after the reserve the output is the input, bit-exact */
    g_sine = 0; jit_init(&J, TARGET); jit_start(&J); run(1000, 400, d_inorder, 0);
    long s = first_sound();
    long lat = -1;                                                                        /* latency: output frame of input frame 300*192 */
    for (long i = 0; i < nout; i++) if (OUT[2 * i] == sig_l(300L * N) && OUT[2 * i + 1] == sig_r(300L * N)) { lat = i - 300L * N; break; }
    int exact = 1;
    for (long i = 0; i < nout; i++) { long f = i - lat; if (i > s + JIT_XF_START && f >= 0 && f < 400L * N && (OUT[2 * i] != sig_l(f) || OUT[2 * i + 1] != sig_r(f))) { exact = 0; break; } }
    printf("     reserve reached after %.1f ms, latency %.1f ms (target 80)\n", s / 48.0, lat / 48.0);
    check(exact, "in order: output equals the input after the fade-in");
    check(lat >= TARGET - N && lat <= TARGET + N, "in order: latency = target (the newest packet counts in full)");
    check(J.st.missing == 0 && J.st.underruns == 0 && J.st.concealed == 0, "in order: nothing concealed");

    /* 2. swapped pairs and duplicates: still bit-exact */
    jit_init(&J, TARGET); jit_start(&J); run(2000, 400, d_swap, 0);
    exact = 1; s = first_sound();
    long lat2 = -1; for (long i = 0; i < nout; i++) if (OUT[2 * i] == sig_l(300L * N) && OUT[2 * i + 1] == sig_r(300L * N)) { lat2 = i - 300L * N; break; }
    for (long i = s + JIT_XF_START; i < nout; i++) { long f = i - lat2; if (f >= 0 && (OUT[2 * i] != sig_l(f) || OUT[2 * i + 1] != sig_r(f))) { exact = 0; break; } }
    check(exact && J.st.missing == 0 && J.st.late == 0, "pairs arriving swapped: bit-exact, nothing missing");
    jit_init(&J, TARGET); jit_start(&J);
    nout = 0; for (int k = 0; k < 300; k++) { push((uint16_t)(3000 + k), k); push((uint16_t)(3000 + k), k); pull(N); }
    check(J.st.dup == 300, "every packet twice: duplicates counted and ignored");

    /* 3. one lost packet in a sine: mirrored repetition, no click */
    g_sine = 1;
    jit_init(&J, TARGET); jit_start(&J); run(4000, 300, d_inorder, 0); int normal = max_step(2000, nout);
    jit_init(&J, TARGET); jit_start(&J); run(4000, 300, d_lost, 0);
    int lost1 = max_step(2000, nout);
    printf("     largest step: normal %d, with one lost packet %d (plain silence would jump up to 10000)\n", normal, lost1);
    check(J.st.missing == 1 && J.st.concealed == 1, "one lost packet: counted and bridged");
    check(lost1 <= normal * 2, "one lost packet: no click (step at most 2x the normal)");
    jit_init(&J, TARGET); jit_start(&J); run(4000, 300, d_lost3, 0);
    int lost3 = max_step(2000, nout);
    printf("     three lost packets in a row: largest step %d\n", lost3);
    check(J.st.concealed == 3 && lost3 <= normal * 2, "three lost packets: bridged, no click");

    /* 4. a 60 ms WLAN stall is covered by the 80 ms reserve: nothing missing, output bit-exact */
    g_sine = 0; jit_init(&J, TARGET); jit_start(&J); run(5000, 300, d_burst, 0);
    check(J.st.missing == 0 && J.st.underruns == 0, "60 ms stall: covered by the reserve");

    /* 5. a 200 ms stall: runs empty, fades, collects the reserve again, goes on without a click */
    g_sine = 1; jit_init(&J, TARGET); jit_start(&J); run(6000, 300, d_gap, 0);
    int gap = max_step(2000, nout);
    printf("     200 ms stall: underruns %u, concealed %u, silent %u, prefills %u, largest step %d\n",
           J.st.underruns, J.st.concealed, J.st.silent, J.st.prefills, gap);
    check(J.st.underruns >= 1 && J.st.prefills == 2 && gap <= normal * 2, "200 ms stall: fades out, refills, fades in");
    check(jit_playing(&J), "200 ms stall: playing again afterwards");

    /* 6. a packet that comes long after its time is dropped as late */
    g_sine = 0; jit_init(&J, TARGET); jit_start(&J); run(7000, 300, d_late, 0);
    check(J.st.late == 1 && J.st.missing == 1, "very late packet: bridged when due, dropped when it comes");

    /* 7. sequence wraps 65535 -> 0 */
    jit_init(&J, TARGET); jit_start(&J); run(65400, 400, d_inorder, 0);
    exact = 1; s = first_sound();
    long lat3 = -1; for (long i = 0; i < nout; i++) if (OUT[2 * i] == sig_l(300L * N) && OUT[2 * i + 1] == sig_r(300L * N)) { lat3 = i - 300L * N; break; }
    for (long i = s + JIT_XF_START; i < nout; i++) { long f = i - lat3; if (f >= 0 && (OUT[2 * i] != sig_l(f) || OUT[2 * i + 1] != sig_r(f))) { exact = 0; break; } }
    check(exact && J.st.restarts == 0 && J.st.missing == 0, "sequence wrap 65535 -> 0: seamless");

    /* 8. jump in the sequence (new stream / other source): start over with fades */
    g_sine = 1; jit_init(&J, TARGET); jit_start(&J);
    nout = 0; for (int k = 0; k < 200; k++) { push((uint16_t)(100 + k), k); pull(N); }
    for (int k = 0; k < 200; k++) { push((uint16_t)(9000 + k), k); pull(N); }
    check(J.st.restarts == 1 && jit_playing(&J) && max_step(2000, nout) <= normal * 2, "sequence jump: restart with fade out / in");

    /* 8b. new source whose numbers are a little behind: runs empty once, then starts over on the new numbers */
    jit_init(&J, TARGET); jit_start(&J);
    nout = 0; for (int k = 0; k < 200; k++) { push((uint16_t)(3000 + k), k); pull(N); }
    for (int k = 0; k < 200; k++) { push((uint16_t)(3150 + k), k); pull(N); }
    printf("     restarts %u late %u state %d underruns %u\n", J.st.restarts, J.st.late, J.state, J.st.underruns);
    check(J.st.restarts == 1 && jit_playing(&J) && J.st.late < 30, "new source 50 packets behind: starts over instead of dropping everything as late");

    /* 9. far too much waiting (300 ms at once at the start): packets are dropped down to the target */
    jit_init(&J, TARGET); jit_start(&J);
    nout = 0; for (int k = 0; k < 75; k++) push((uint16_t)(20000 + k), k);
    for (int k = 75; k < 1000; k++) { push((uint16_t)(20000 + k), k); pull(N); }
    printf("     300 ms at once: %u packets dropped, waiting now %.1f ms, largest step %d\n", J.st.skipped, jit_fill(&J) / 48.0, max_step(2000, nout));
    check(J.st.skipped > 0 && jit_fill(&J) < TARGET + JIT_SKIP_MARGIN + 2 * N && max_step(2000, nout) <= normal * 2, "overfill: dropped with crossfades, no click");

    /* 10. stop fades out within the start/stop fade and stays silent */
    jit_init(&J, TARGET); jit_start(&J); nout = 0;
    for (int k = 0; k < 100; k++) { push((uint16_t)k, k); pull(N); }
    long at = nout; jit_stop(&J); pull(2 * N);
    int silent_after = 1; for (long i = at + JIT_XF_START; i < nout; i++) if (OUT[2 * i] || OUT[2 * i + 1]) silent_after = 0;
    check(silent_after && max_step(at - 10, nout) <= normal * 2, "stop: fade out, then silence");
    push(500, 0); check(jit_fill(&J) == 0, "stopped: packets are ignored");

    printf(fails ? "\n%d FAILED\n" : "\nall ok\n", fails);
    return fails != 0;
}
