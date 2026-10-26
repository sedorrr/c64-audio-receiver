/* Jitter buffer for the Ultimate's audio stream - see jitter.h */
#include <string.h>
#include "jitter.h"

#define N      U64_PKT_FRAMES
#define G1     65536                            /* gain 1.0 in Q16 */
enum { JIT_SRC_SILENCE, JIT_SRC_REAL, JIT_SRC_REPEAT };
static const int32_t REPEAT_GAIN[JIT_MAX_REPEAT + 1] = { G1, G1 * 7 / 10, G1 * 4 / 10, 0 };

static int slot(uint16_t seq) { return seq % JIT_SLOTS; }
static bool have(const jit_t *j, uint16_t seq) { int s = slot(seq); return j->valid[s] && j->tag[s] == seq; }

void jit_init(jit_t *j, int target_frames) {
    memset(j, 0, sizeof *j);
    j->target = target_frames;
    j->state = JIT_IDLE;
}
void jit_set_target(jit_t *j, int target_frames) { j->target = target_frames; }

int jit_fill(const jit_t *j) {
    if (j->state == JIT_IDLE || j->need_base) return 0;
    int f = seq_diff(j->head, j->play_seq) * N - (j->state == JIT_PLAY ? j->play_off : 0);
    return f > 0 ? f : 0;
}

/* ---- voices: a packet played forwards or backwards with a gain ramp; mirrors at its ends ---- */
static void voice(jit_voice_t *v, const int16_t *p, int pos, int dir, int32_t g0, int32_t g1) {
    int32_t d = g1 - g0;                        /* rounded down: a fade to 0 really ends at 0 */
    v->p = p; v->pos = pos; v->dir = dir; v->g = g0; v->dg = d >= 0 ? d / N : -((-d + N - 1) / N);
}
static void vnext(jit_voice_t *v, int32_t *l, int32_t *r) {
    if (!v->p) { *l = *r = 0; return; }
    const int16_t *f = v->p + 2 * v->pos;
    *l = (int32_t)(((int64_t)f[0] * v->g) >> 16);
    *r = (int32_t)(((int64_t)f[1] * v->g) >> 16);
    v->g += v->dg;
    if (v->g < 0) { v->g = 0; v->dg = 0; }
    v->pos += v->dir;
    if (v->pos >= N) { v->pos = N - 1; v->dir = -1; }
    else if (v->pos < 0) { v->pos = 0; v->dir = 1; }
}
/* the current sound fades out over `len` frames while `cur` (set by the caller) fades in */
static void crossfade_from_current(jit_t *j, int len, bool copy) {
    j->old = j->cur;
    if (copy && j->old.p) {                     /* its slot may be overwritten while it fades */
        memcpy(j->fade, j->old.p, sizeof j->fade);
        j->old.p = j->fade;
    }
    j->xf_left = j->xf_len = len;
}

void jit_start(jit_t *j) {
    if (j->state != JIT_IDLE) return;
    j->state = JIT_PREFILL; j->need_base = true; j->fresh = true;
    j->miss_run = 0; j->last_src = JIT_SRC_SILENCE; j->fill_avg = 0;
    j->st.prefills++;
}
void jit_stop(jit_t *j) {
    if (j->state == JIT_IDLE) return;
    crossfade_from_current(j, JIT_XF_START, true);
    voice(&j->cur, NULL, 0, 1, 0, 0);
    j->state = JIT_IDLE;
}

void jit_push(jit_t *j, uint16_t seq, const uint8_t *pcm_le) {
    if (j->state == JIT_IDLE) return;
    j->st.packets++;
    if (j->need_base) { j->play_seq = seq; j->head = seq; j->play_off = 0; j->need_base = false; }
    int d = seq_diff(seq, j->play_seq);
    if (d < 0 && j->state == JIT_PREFILL && j->fresh && seq_diff(j->head, seq) < JIT_AHEAD_MAX) {
        j->play_seq = seq; d = 0;               /* arrived out of order right at the start: start earlier */
    }
    /* far ahead: a new stream (or another source) - start over. The same if, while collecting the reserve after
       running empty, packets come from well before the play position: another source whose numbers are behind */
    bool behind = d < -(JIT_MAX_REPEAT + 1) && j->state == JIT_PREFILL && !j->fresh;
    if (d < 0 && !behind) { j->st.late++; return; }
    if (d >= JIT_AHEAD_MAX || behind) {
        j->st.restarts++;
        if (j->state == JIT_PLAY) crossfade_from_current(j, JIT_XF_START, true);
        voice(&j->cur, NULL, 0, 1, 0, 0);
        memset(j->valid, 0, sizeof j->valid);
        j->state = JIT_PREFILL; j->fresh = true; j->miss_run = 0; j->last_src = JIT_SRC_SILENCE;
        j->play_seq = seq; j->head = seq; j->play_off = 0; j->fill_avg = 0;
        j->st.prefills++;
    }
    int s = slot(seq);
    if (j->valid[s] && j->tag[s] == seq) { j->st.dup++; return; }
    int16_t *dst = j->pcm[s];
    for (int i = 0; i < 2 * N; i++) dst[i] = (int16_t)(pcm_le[2 * i] | pcm_le[2 * i + 1] << 8);
    j->tag[s] = seq; j->valid[s] = 1;
    if (seq_diff(seq, j->head) >= 0) j->head = (uint16_t)(seq + 1);
}

/* start of a packet slot: decide what it plays */
static void begin_packet(jit_t *j) {
    uint16_t seq = j->play_seq;
    if (have(j, seq)) {
        int fill = jit_fill(j);
        j->fill_avg += ((float)fill - j->fill_avg) / 32.0f;
        if (j->fill_avg > (float)(j->target + JIT_SKIP_MARGIN) && j->last_src == JIT_SRC_REAL
            && have(j, (uint16_t)(seq + 1))) {  /* far too much waiting: drop this packet, crossfade into the next */
            voice(&j->cur, j->pcm[slot(seq)], 0, 1, G1, G1);
            crossfade_from_current(j, JIT_XF, false);
            j->st.skipped++; j->fill_avg -= N;
            seq = ++j->play_seq;
        } else if (j->last_src == JIT_SRC_REPEAT) {
            crossfade_from_current(j, JIT_XF, false);       /* the repetition goes on mirrored while B fades in */
        } else if (j->last_src == JIT_SRC_SILENCE) {
            crossfade_from_current(j, JIT_XF_START, false);  /* fade in */
        }
        voice(&j->cur, j->pcm[slot(seq)], 0, 1, G1, G1);
        j->last_src = JIT_SRC_REAL; j->last_real = seq; j->miss_run = 0;
        return;
    }
    bool newer = seq_diff(j->head, seq) > 0;
    if (newer) j->st.missing++; else j->st.underruns++;
    j->miss_run++;
    if (j->miss_run == 1 && j->last_src == JIT_SRC_REAL)    /* the slot may be reused later: keep a copy */
        memcpy(j->rep, j->pcm[slot(j->last_real)], sizeof j->rep);
    if (j->miss_run <= JIT_MAX_REPEAT && j->last_src != JIT_SRC_SILENCE) {
        bool back = j->miss_run & 1;            /* backwards, forwards, backwards: continuous at every joint */
        voice(&j->cur, j->rep, back ? N - 1 : 0, back ? -1 : 1, REPEAT_GAIN[j->miss_run - 1], REPEAT_GAIN[j->miss_run]);
        j->last_src = JIT_SRC_REPEAT;
        j->st.concealed++;
        return;
    }
    voice(&j->cur, NULL, 0, 1, 0, 0);           /* faded to 0 by the last repetition */
    j->last_src = JIT_SRC_SILENCE;
    j->st.silent++;
    if (!newer) {                               /* the reserve ran out: collect it again before going on */
        j->state = JIT_PREFILL; j->play_off = 0;
        j->st.prefills++;
    }
}

/* PREFILL -> PLAY once the reserve is there. Only packets that really arrived count: packets that never
   came (lost in a long WLAN gap) are not waited for, and a burst that arrives out of order is not started
   before its gaps have filled. */
static void try_start(jit_t *j) {
    if (j->need_base) return;
    int got = 0;
    for (uint16_t s = j->play_seq; seq_diff(j->head, s) > 0; s++) got += have(j, s);
    if (got * N < j->target) return;
    /* more than the reserve arrived at once (a burst after a stall): start with the reserve, not more,
       otherwise the extra delay would stay until the clock control has worked it off */
    uint16_t first = j->play_seq;
    for (int extra = (got * N - j->target) / N; ; first++, extra--, j->st.skipped++) {
        while (!have(j, first)) first++;
        if (extra <= 0) break;
    }
    j->play_seq = first;
    j->state = JIT_PLAY; j->play_off = 0; j->fresh = false; j->miss_run = 0;
    j->last_src = JIT_SRC_SILENCE;
    /* start a few frames into the first packet, so exactly `target` frames wait (steady pitch from the start) */
    int skip = seq_diff(j->head, first) * N - j->target;
    if (skip > 0 && skip < N) { begin_packet(j); j->cur.pos = skip; j->play_off = skip; }
    j->fill_avg = (float)jit_fill(j);
}

void jit_pull(jit_t *j, int16_t *out, int frames) {
    if (j->state == JIT_PREFILL) try_start(j);  /* once per call: playing starts at most a tick later */
    for (int i = 0; i < frames; i++) {
        if (j->state == JIT_PLAY && j->play_off == 0) begin_packet(j);   /* (not again if try_start began mid-packet) */
        int32_t l = 0, r = 0;
        if (j->state == JIT_PLAY) vnext(&j->cur, &l, &r);
        if (j->xf_left > 0) {                   /* crossfade: old fades out, new fades in (linear) */
            int32_t ol, orr;
            vnext(&j->old, &ol, &orr);
            int32_t w = (int32_t)(((int64_t)(j->xf_len - j->xf_left) << 16) / j->xf_len);
            l = ol + (int32_t)(((int64_t)(l - ol) * w) >> 16);
            r = orr + (int32_t)(((int64_t)(r - orr) * w) >> 16);
            j->xf_left--;
        }
        out[2 * i]     = (int16_t)(l > 32767 ? 32767 : (l < -32768 ? -32768 : l));
        out[2 * i + 1] = (int16_t)(r > 32767 ? 32767 : (r < -32768 ? -32768 : r));
        if (j->state == JIT_PLAY && ++j->play_off == N) { j->play_off = 0; j->play_seq++; }
    }
}
