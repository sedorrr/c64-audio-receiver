/*
 * WLAN part of the audio receiver (Pico 2 W): settings, web page, the Ultimate's REST API, the audio stream
 * (receive, hand to the player, forward to the visualizer).
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>

#define U64_MAX 4                               /* Ultimates that can be chosen as the source */

void net_load_settings(void);                   /* first: reads the flash (player_init needs latency + volume) */
int  net_latency_ms(void);
int  net_volume(void);
bool net_play_wanted(void);
void net_init(void);                            /* start WLAN (non-blocking) */
void net_task(void);                            /* main loop: connection, stream requests, saving */

enum { NET_LED_OFF, NET_LED_CONNECTING, NET_LED_AP, NET_LED_IDLE, NET_LED_PLAYING };
int  net_led(void);                             /* what the on-board LED should show */
void net_toggle_play(void);                     /* button: short press */
void net_next_source(void);                     /* button: long press */
