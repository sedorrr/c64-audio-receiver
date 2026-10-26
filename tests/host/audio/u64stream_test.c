/* Packet parser, sequence arithmetic, source lock and forwarding rule */
#include <stdio.h>
#include <string.h>
#include "u64stream.h"

static int fails = 0;
static void check(int ok, const char *what) { printf("%s %s\n", ok ? "ok  " : "FAIL", what); if (!ok) fails++; }

int main(void) {
    uint8_t pkt[U64_PKT_BYTES] = { 0x57, 0x83 };                       /* 0x8357 = 33623, as captured from the Ultimate */
    pkt[2] = 0x10; pkt[3] = 0x20;
    u64pkt_t p;
    check(u64pkt_parse(pkt, sizeof pkt, &p) && p.seq == 33623 && p.pcm == pkt + 2, "770-byte packet: sequence little endian, audio after it");
    check(!u64pkt_parse(pkt, 2, &p), "2-byte start packet of the Ultimate: not audio");
    check(!u64pkt_parse(pkt, 769, &p) && !u64pkt_parse(pkt, 771, &p) && !u64pkt_parse(NULL, 770, &p), "wrong sizes: not audio");
    check(seq_diff(1, 65535) == 2 && seq_diff(65535, 1) == -2 && seq_diff(100, 100) == 0, "sequence distance across the wrap");

    srclock_t s; srclock_reset(&s);
    uint32_t U64 = 0x01020304, PC = 0x05060708;
    check(srclock_accept(&s, U64, 0), "first source is taken");
    check(srclock_accept(&s, U64, 4000), "same source again");
    check(!srclock_accept(&s, PC, 8000), "second source while the first is live: rejected (no mixing)");
    check(!srclock_accept(&s, PC, 4000 + SRC_HOLD_US - 1), "...still rejected just before the hold time");
    check(srclock_accept(&s, PC, 4000 + SRC_HOLD_US), "first source silent for the hold time: second takes over");
    check(s.switches == 1 && s.rejected == 2, "switches and rejected packets counted");
    check(SRC_HOLD_US < 250000u, "hold time below the visualizer's 250 ms (else it asks for the stream during a switch)");
    check(srclock_accept(&s, PC, 0xFFFFFF00u) && srclock_accept(&s, PC, 0x100), "time stamp wrap does not matter");

    uint32_t ME = 0x0A0A0A0A, VIZ = 0x0B0B0B0B;
    check(fwd_wanted(true, VIZ, U64, ME), "forward packets from the Ultimate to the visualizer");
    check(!fwd_wanted(false, VIZ, U64, ME), "forwarding off");
    check(!fwd_wanted(true, 0, U64, ME), "no target address (not resolved yet)");
    check(!fwd_wanted(true, VIZ, VIZ, ME), "never back to the device the packet came from (no loop)");
    check(!fwd_wanted(true, ME, U64, ME), "never to ourselves");

    printf(fails ? "\n%d FAILED\n" : "\nall ok\n", fails);
    return fails != 0;
}
