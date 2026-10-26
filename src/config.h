/*
 * C64 audio receiver (Pico 2 W + PCM5102A) - pins and constants.
 * The pins are chosen so they do not collide with the C64 visualizer (GP4/5, GP14-19, GP26/27):
 * the receiver can later move into the matrix Pico unchanged.
 */
#pragma once

/* ---- pins (physical pin numbers of the Pico 2 W in brackets) ---- */
#define PIN_I2S_DIN    20   /* PCM5102A DIN                          (26) */
#define PIN_I2S_BCK    21   /* PCM5102A BCK                          (27) */
#define PIN_I2S_LRCK   22   /* PCM5102A LCK - must be BCK + 1 (PIO side-set) (29) */
#define PIN_BUTTON     15   /* optional button to GND (internal pull-up) (20) */

/* ---- identity ---- */
#define DEVICE_NAME_DEFAULT "c64audio"   /* <name>.local, DHCP host name; access point C64-Audio[-<name>] */
#define FW_MARKER           "c64audio-fw" /* the web update only takes firmware that contains this text */

/* ---- Ultimate audio stream ---- */
#define AUDIO_PORT        11001      /* UDP port of the Ultimate's audio stream */
#define U64_RATE_NOMINAL  47983.0    /* measured: 47982-47983 Hz (not 48000) */

/* ---- playback ---- */
#define LATENCY_DEFAULT_MS  80       /* jitter buffer target (packet arrival -> DAC) */
#define LATENCY_MIN_MS      40
#define LATENCY_MAX_MS      300
#define VOLUME_DEFAULT      80       /* percent */

/* ---- flash layout (4 MB) ---- */
#define OTA_STAGE_OFFSET   0x200000u                                    /* firmware update staging area */
#define NETCFG_OFFSET      (PICO_FLASH_SIZE_BYTES - 2 * 4096)           /* WLAN/Ultimate/web settings (same place as the visualizer) */
#define PREFS_OFFSET       (PICO_FLASH_SIZE_BYTES - 1 * 4096)           /* volume, source, play (wear-levelled) */
