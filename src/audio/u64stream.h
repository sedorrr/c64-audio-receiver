/*
 * The Ultimate 64's audio stream - packet format, source lock and forwarding rule. Portable C (host-tested).
 *
 * Packet (UDP, measured on an Ultimate 64 Elite II, FW 3.15a):
 *   2 bytes  sequence number, little endian, +1 per packet (continues across stream starts)
 *   192 x    stereo frame: left int16, right int16 (little endian), ~47983 Hz -> ~250 packets/s
 * At a stream start the Ultimate also sends a 2-byte packet without audio; it is not used for playback.
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>

#define U64_PKT_FRAMES  192
#define U64_PKT_BYTES   (2 + 4 * U64_PKT_FRAMES)       /* 770 */

typedef struct {
    uint16_t       seq;
    const uint8_t *pcm;                                 /* U64_PKT_FRAMES x (L, R) int16 little endian */
} u64pkt_t;

/* true for a full audio packet; markers, short or odd-sized packets are not audio */
bool u64pkt_parse(const uint8_t *data, int len, u64pkt_t *p);

/* signed distance a - b of two 16-bit sequence numbers (handles the wrap at 65535 -> 0) */
static inline int seq_diff(uint16_t a, uint16_t b) { return (int16_t)(uint16_t)(a - b); }

/* Source lock: packets may come from the Ultimate itself or from a device that forwards its stream
   (the PC jukebox, another receiver). Two streams must never be mixed: the receiver sticks to the
   source it hears; another source takes over only after the current one was silent for SRC_HOLD_US.
   Only audio packets count: the Elite II sends its 2-byte start packet from its WLAN address (.202) and
   the stream from its Ethernet address (.168) - locking onto the start packet rejected the real stream
   for the whole hold time (found on the device, 2026-10-14). Two live streams alternate every ~4 ms, so
   200 ms keeps them apart just as well and stays below the visualizer's 250 ms before it asks again. */
#define SRC_HOLD_US 200000u
typedef struct {
    uint32_t ip;                                        /* current source, 0 = none yet */
    uint32_t last_us;                                   /* time of its last packet */
    uint32_t switches, rejected;
} srclock_t;
void srclock_reset(srclock_t *s);
bool srclock_accept(srclock_t *s, uint32_t ip, uint32_t now_us);   /* use this packet? */

/* Forward a packet to the visualizer? Never back to the device it came from (two receivers that
   forward to each other would bounce packets forever) and never to ourselves. */
static inline bool fwd_wanted(bool enabled, uint32_t target_ip, uint32_t from_ip, uint32_t my_ip) {
    return enabled && target_ip != 0 && target_ip != from_ip && target_ip != my_ip;
}
