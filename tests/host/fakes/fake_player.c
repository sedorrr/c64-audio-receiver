/* fake of player.c: records what the network part asks for (the real one drives PIO, DMA and a timer) */
#include <string.h>
#include "player.h"
int FP_PACKETS = 0, FP_LAST_SEQ = -1, FP_PLAY = -1, FP_VOL = -1, FP_RESTARTS = 0, FP_FLASH = 0, FP_STATE = JIT_IDLE;
void player_init(int target_ms, int volume) { (void)target_ms; FP_VOL = volume; }
void player_packet(uint16_t seq, const uint8_t *pcm) { (void)pcm; FP_PACKETS++; FP_LAST_SEQ = seq; }
void player_play(bool on) { FP_PLAY = on; FP_STATE = on ? JIT_PLAY : JIT_IDLE; }
void player_restart(void) { FP_RESTARTS++; }
void player_set_volume(int pct) { FP_VOL = pct; }
void player_reset_stats(void) {}
void player_stats(player_stats_t *s) { memset(s, 0, sizeof *s); s->state = FP_STATE; s->volume = FP_VOL; s->target_ms = 80; s->rate = 47983.0; }
uint32_t player_flash_begin(bool erase) { (void)erase; FP_FLASH++; return 0; }
void player_flash_end(uint32_t irq) { (void)irq; }
void i2s_out_stop(void) {}
