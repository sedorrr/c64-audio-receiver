/* I2S output to the PCM5102A - see i2s_out.h */
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/sync.h"
#include "config.h"
#include "i2s_out.h"
#include "i2s.pio.h"

#define RING_BYTES  (I2S_RING_FRAMES * 8)                 /* 32 KB: the largest DMA ring */
#define ZERO_AHEAD  1024                                  /* frames kept silent in front of the writer (21 ms) */
_Static_assert(RING_BYTES == 1 << 15, "DMA ring size must be a power of two (2^15)");

static uint32_t ring[I2S_RING_FRAMES * 2] __attribute__((aligned(RING_BYTES)));
static PIO      pio;
static uint     sm, dma_ch;
static uint32_t w_pos, z_pos;                             /* free-running frame counters: written, zeroed up to */
static uint32_t r_pos;                                    /* free-running read position (frames) */
static uint64_t r_time;                                   /* time of the last read-position update */
static uint32_t behind;

void i2s_out_init(uint32_t div_q8) {
    uint offset;
    hard_assert(pio_claim_free_sm_and_add_program_for_gpio_range(&i2s32_program, &pio, &sm, &offset, PIN_I2S_DIN, 3, true));
    i2s32_program_init(pio, sm, offset, PIN_I2S_DIN, PIN_I2S_BCK);
    i2s_out_set_div(div_q8);

    dma_ch = dma_claim_unused_channel(true);
    dma_channel_config c = dma_channel_get_default_config(dma_ch);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, false);
    channel_config_set_ring(&c, false, 15);               /* read address wraps every 32 KB */
    channel_config_set_dreq(&c, pio_get_dreq(pio, sm, true));
    dma_channel_configure(dma_ch, &c, &pio->txf[sm], ring,
                          (DMA_CH0_TRANS_COUNT_MODE_VALUE_ENDLESS << DMA_CH0_TRANS_COUNT_MODE_LSB) | 1u, true);
    pio_sm_set_enabled(pio, sm, true);
    r_time = time_us_64();
}

void i2s_out_set_div(uint32_t div_q8) {
    if (div_q8 < 0x200) div_q8 = 0x200;
    pio_sm_set_clkdiv_int_frac8(pio, sm, div_q8 >> 8, (uint8_t)div_q8);
}

/* read position: the DMA's read address inside the ring, made free-running. Between two calls the DMA can
   wrap more than once only if we were blocked for > 85 ms - the elapsed time says how often. */
static void update_read(void) {
    uint32_t idx = (((uint32_t)dma_hw->ch[dma_ch].read_addr - (uint32_t)ring) / 8) % I2S_RING_FRAMES;
    uint64_t now = time_us_64();
    uint32_t delta = (idx - r_pos) % I2S_RING_FRAMES;
    uint64_t expect = (now - r_time) * 48u / 1000u;                 /* frames at ~48 kHz */
    if (expect > I2S_RING_FRAMES / 2)
        delta += (uint32_t)((expect - delta + I2S_RING_FRAMES / 2) / I2S_RING_FRAMES) * I2S_RING_FRAMES;
    r_pos += delta;
    r_time = now;
}

int i2s_out_queued(void) {
    update_read();
    int q = (int)(w_pos - r_pos);
    if (q < 0) {                                          /* fell behind: the DAC has been playing the silence */
        if (w_pos) behind++;                              /* (not at the start, before anything was written) */
        w_pos = r_pos + 16;                               /* continue just in front of it */
        q = 16;
    }
    return q;
}

void i2s_out_write(const int32_t *lr, int frames) {
    for (int i = 0; i < frames; i++) {
        uint32_t k = (w_pos++ % I2S_RING_FRAMES) * 2;
        ring[k] = (uint32_t)lr[2 * i];
        ring[k + 1] = (uint32_t)lr[2 * i + 1];
    }
    if ((int32_t)(z_pos - w_pos) < 0) z_pos = w_pos;
    for (; (int32_t)(z_pos - w_pos) < ZERO_AHEAD; z_pos++) {
        uint32_t k = (z_pos % I2S_RING_FRAMES) * 2;
        ring[k] = ring[k + 1] = 0;
    }
}

uint32_t i2s_out_behind(void) { return behind; }

void i2s_out_silence_rest(void) {
    update_read();
    for (uint32_t k = w_pos; (int32_t)(k - (r_pos + I2S_RING_FRAMES - 64)) < 0; k++) {   /* up to just before the DMA */
        uint32_t i = (k % I2S_RING_FRAMES) * 2;
        ring[i] = ring[i + 1] = 0;
    }
    z_pos = r_pos + I2S_RING_FRAMES - 64;
}

void i2s_out_stop(void) {
    dma_channel_abort(dma_ch);
    pio_sm_set_enabled(pio, sm, false);
    pio_sm_set_pins_with_mask(pio, sm, 0, (1u << PIN_I2S_DIN) | (3u << PIN_I2S_BCK));
}
