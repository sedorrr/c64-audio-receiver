/*
 * I2S output to the PCM5102A: PIO state machine fed by one DMA channel that reads a 32 KB ring forever
 * (RP2350 "endless" transfer mode) - no interrupt per block. The ring holds 4096 stereo frames (85 ms);
 * the player writes a few milliseconds ahead of the DMA read position and changes the PIO clock divider
 * to follow the Ultimate's clock. Frames already played are zeroed behind the writer, so if the writer ever
 * falls behind (e.g. a long flash erase) the DAC plays silence, not old audio.
 */
#pragma once
#include <stdint.h>

#define I2S_RING_FRAMES 4096

void     i2s_out_init(uint32_t div_q8);              /* PIO divider (16.8) for the start rate */
int      i2s_out_queued(void);                       /* frames written but not played yet (0 if the writer fell behind) */
void     i2s_out_write(const int32_t *lr, int frames); /* append frames (L, R as 32-bit words) */
void     i2s_out_set_div(uint32_t div_q8);           /* PIO clock divider, 16.8 fixed point */
uint32_t i2s_out_behind(void);                       /* how often the writer fell behind the DMA (should stay 0) */
void     i2s_out_silence_rest(void);                 /* zero everything between the writer and the DMA (before a long stall) */
void     i2s_out_stop(void);                         /* stop DMA and PIO for good (firmware update: no looping buzz) */
