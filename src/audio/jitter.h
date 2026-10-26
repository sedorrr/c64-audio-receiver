/*
 * Jitter buffer for the Ultimate's audio stream. Portable C, single context (host-tested).
 *
 * Packets are stored by sequence number, so packets that arrive out of order or twice do no harm.
 * Playback starts once `target` frames are waiting and then reads frame by frame at the DAC's rate;
 * the clock controller (clockctl.h) keeps the amount waiting at the target.
 *
 * Missing packets are hidden instead of clicking:
 *  - a packet that is missing when it is due is bridged by playing the last good packet again,
 *    mirrored in time (backwards, forwards, backwards). Mirroring keeps the waveform continuous at
 *    every joint, and the level fades over the repetitions (1 -> 0.7 -> 0.4 -> 0);
 *  - when real packets come back, the repetition crossfades into them (~1.3 ms);
 *  - after more than JIT_MAX_REPEAT missing packets it is silent; if the buffer ran empty
 *    (a WLAN gap longer than the reserve), it collects the reserve again before it goes on;
 *  - far too much waiting (e.g. after a burst) drops whole packets with a crossfade;
 *  - start, stop and a jump in the sequence (new stream, other source) fade in / out.
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "u64stream.h"

#define JIT_SLOTS       128                     /* packets of 192 frames: 512 ms */
#define JIT_XF          64                      /* crossfade between sources, frames (1.3 ms) */
#define JIT_XF_START    256                     /* fade in / out at start, stop, restart (5.3 ms) */
#define JIT_MAX_REPEAT  3                       /* missing packets bridged by repetition (12 ms) */
#define JIT_SKIP_MARGIN (48 * 40)               /* frames above the target before packets are dropped (40 ms) */
#define JIT_AHEAD_MAX   (JIT_SLOTS - 4)         /* packets further ahead than this mean a new stream */

enum { JIT_IDLE, JIT_PREFILL, JIT_PLAY };

typedef struct {
    uint32_t packets;      /* audio packets taken in */
    uint32_t late;         /* arrived after their time had passed (dropped) */
    uint32_t dup;          /* arrived twice */
    uint32_t missing;      /* not there when due although newer packets were (lost or very late) */
    uint32_t underruns;    /* not there when due and nothing newer either: the reserve ran out */
    uint32_t concealed;    /* packets bridged by repetition */
    uint32_t silent;       /* packets played as silence (more than JIT_MAX_REPEAT missing in a row) */
    uint32_t skipped;      /* dropped because far too much was waiting */
    uint32_t restarts;     /* sequence jumped (new stream / source): started over */
    uint32_t prefills;     /* times the reserve was collected (start + after running empty) */
} jit_stats_t;

typedef struct {                                /* one playing source (a packet, possibly repeated) */
    const int16_t *p;                           /* interleaved L/R frames, NULL = silence */
    int     pos, dir;                           /* next frame; +1 forwards, -1 backwards (mirrors at the ends) */
    int32_t g, dg;                              /* gain Q16 and its change per frame */
} jit_voice_t;

typedef struct {
    int16_t  pcm[JIT_SLOTS][U64_PKT_FRAMES * 2];
    uint16_t tag[JIT_SLOTS];
    uint8_t  valid[JIT_SLOTS];
    int      state;
    bool     need_base;                         /* PREFILL: the next packet sets the play position */
    bool     fresh;                             /* nothing played since the start: earlier packets may still move the start */
    uint16_t play_seq;                          /* packet being played */
    int      play_off;                          /* next frame in it */
    uint16_t head;                              /* newest packet + 1 */
    int      target;                            /* frames waiting before playback starts */
    int      last_src;                          /* what the previous packet slot played (JIT_SRC_*) */
    int      miss_run;                          /* missing packets in a row */
    uint16_t last_real;                         /* sequence number of the last real packet played */
    float    fill_avg;                          /* smoothed amount waiting (for dropping packets) */
    jit_voice_t cur, old;                       /* playing now / fading out */
    int      xf_left, xf_len;
    int16_t  rep[U64_PKT_FRAMES * 2];           /* copy of the last good packet (repetition) */
    int16_t  fade[U64_PKT_FRAMES * 2];          /* copy of whatever is faded out at a stop / restart */
    jit_stats_t st;
} jit_t;

void jit_init(jit_t *j, int target_frames);
void jit_set_target(jit_t *j, int target_frames);
void jit_start(jit_t *j);                       /* IDLE -> collect the reserve, then play */
void jit_stop(jit_t *j);                        /* fade out, then silence */
/* one packet: seq + U64_PKT_FRAMES stereo frames as in the stream (int16 little endian) */
void jit_push(jit_t *j, uint16_t seq, const uint8_t *pcm_le);
/* `frames` stereo frames for the DAC (interleaved L, R) */
void jit_pull(jit_t *j, int16_t *out, int frames);
int  jit_fill(const jit_t *j);                  /* frames from the play position to the newest packet */
static inline bool jit_playing(const jit_t *j) { return j->state == JIT_PLAY; }
