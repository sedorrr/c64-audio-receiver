/* The Ultimate 64's audio stream - see u64stream.h */
#include "u64stream.h"

bool u64pkt_parse(const uint8_t *data, int len, u64pkt_t *p) {
    if (!data || len != U64_PKT_BYTES) return false;
    p->seq = (uint16_t)(data[0] | data[1] << 8);
    p->pcm = data + 2;
    return true;
}

void srclock_reset(srclock_t *s) { s->ip = 0; s->last_us = 0; s->switches = 0; s->rejected = 0; }

bool srclock_accept(srclock_t *s, uint32_t ip, uint32_t now_us) {
    if (s->ip && ip != s->ip && (uint32_t)(now_us - s->last_us) < SRC_HOLD_US) { s->rejected++; return false; }
    if (s->ip != ip) { if (s->ip) s->switches++; s->ip = ip; }
    s->last_us = now_us;
    return true;
}
