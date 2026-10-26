/* Player - see player.h */
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/irq.h"
#include "hardware/sync.h"
#include "config.h"
#include "audio/pcore.h"
#include "i2s_out.h"
#include "player.h"

#define QLEN        32                                   /* packets between the network and the timer (128 ms) */
#define FLASH_AHEAD (48 * 60)                            /* queued before a flash write: 60 ms */

static pcore_t P;
static alarm_pool_t *pool;
static repeating_timer_t timer;
static uint64_t last_tick;

/* ---- queue: written by the network (lwIP context), read by the timer interrupt */
static uint8_t  q_pcm[QLEN][4 * U64_PKT_FRAMES];
static uint16_t q_seq[QLEN];
static volatile uint32_t q_w, q_r, q_drop;

void player_packet(uint16_t seq, const uint8_t *pcm_le) {
    uint32_t w = q_w;
    if (w - q_r >= QLEN) { q_drop++; return; }
    memcpy(q_pcm[w % QLEN], pcm_le, sizeof q_pcm[0]);
    q_seq[w % QLEN] = seq;
    __dmb();                                             /* data first, then the index */
    q_w = w + 1;
}

/* ---- requests from the main loop, taken over by the timer */
static volatile int req_play = -1, req_vol = -1;
static volatile bool req_reset, req_restart;
void player_play(bool on) { req_play = on; }
void player_set_volume(int pct) { req_vol = pct; }
void player_reset_stats(void) { req_reset = true; }
void player_restart(void) { req_restart = true; }

static int target_ms, volume;
static player_stats_t snap;                              /* written by the timer, copied with interrupts off */

static void tick(int ahead) {
    uint64_t now = time_us_64();
    float dt = (float)(now - last_tick) * 1e-6f;
    last_tick = now;
    for (uint32_t r = q_r; r != q_w; r++) {
        jit_push(&P.jit, q_seq[r % QLEN], q_pcm[r % QLEN]);
        q_r = r + 1;
    }
    if (req_restart) { if (P.jit.state != JIT_IDLE) { jit_stop(&P.jit); jit_start(&P.jit); } req_restart = false; }
    if (req_play >= 0) { if (req_play) jit_start(&P.jit); else jit_stop(&P.jit); req_play = -1; }   /* after: a stop wins */
    if (req_vol >= 0) { volume = req_vol; pcore_set_volume(&P, volume); req_vol = -1; }
    if (req_reset) { memset(&P.jit.st, 0, sizeof P.jit.st); q_drop = 0; req_reset = false; }
    static int32_t buf[2 * 256];
    int n;
    do {                                                 /* in chunks: more than 256 frames only before a flash write */
        uint32_t div;
        n = pcore_tick(&P, i2s_out_queued(), ahead, buf, 256, dt, &div);
        i2s_out_write(buf, n);
        i2s_out_set_div(div);
        dt = 0;
    } while (n == 256);
    snap.jit = P.jit.st; snap.state = P.jit.state; snap.fill_ms = P.fill_ms; snap.avg_ms = P.clk.fill_avg / 48.0f;
    snap.ppm = P.clk.ppm; snap.rate = clk_rate(&P.clk); snap.qdrop = q_drop; snap.behind = i2s_out_behind();
    snap.target_ms = target_ms; snap.volume = volume;
}
static bool tick_cb(repeating_timer_t *t) { (void)t; tick(PCORE_AHEAD); return true; }

void player_init(int tgt_ms, int vol) {
    target_ms = tgt_ms; volume = vol;
    pcore_init(&P, (double)clock_get_hz(clk_sys), U64_RATE_NOMINAL, tgt_ms * 48);
    pcore_set_volume(&P, vol);
    i2s_out_init(P.div_q8);
    last_tick = time_us_64();
    /* own alarm pool on a free hardware alarm, highest priority: WLAN, web server and flash-free work never delay it */
    pool = alarm_pool_create_with_unused_hardware_alarm(2);
    irq_set_priority(hardware_alarm_get_irq_num(alarm_pool_hardware_alarm_num(pool)), PICO_HIGHEST_IRQ_PRIORITY);
    alarm_pool_add_repeating_timer_us(pool, -1000, tick_cb, NULL, &timer);
}

void player_stats(player_stats_t *s) {
    uint32_t irq = save_and_disable_interrupts();
    *s = snap;
    restore_interrupts(irq);
}

uint32_t player_flash_begin(bool erase) {
    uint32_t irq = save_and_disable_interrupts();
    if (erase) {
        /* the timer cannot run meanwhile - we are it. Take only what is buffered: pulling more would run the
           jitter buffer empty (concealment, then a new reserve) at short latency settings */
        int ahead = i2s_out_queued() + (jit_playing(&P.jit) ? jit_fill(&P.jit) - 48 : FLASH_AHEAD);
        tick(ahead < PCORE_AHEAD ? PCORE_AHEAD : (ahead > FLASH_AHEAD ? FLASH_AHEAD : ahead));
        i2s_out_silence_rest();                          /* an erase that takes longer plays silence, not old audio */
    }
    return irq;
}
void player_flash_end(uint32_t irq) { restore_interrupts(irq); }
