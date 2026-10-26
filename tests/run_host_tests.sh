#!/bin/bash
# Host tests: the portable audio code and the network part (with small fakes of the Pico SDK / lwIP),
# compiled for the PC.   Usage: tests/run_host_tests.sh   (from anywhere)
set -u
cd "$(dirname "$0")/host"
SRC=../../src; B=build
mkdir -p $B
python3 ../../tools/make_web_page.py >/dev/null            # the page the network test serves
W="-O2 -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare -Wno-missing-field-initializers"
AUDIO="$SRC/audio/u64stream.c $SRC/audio/jitter.c $SRC/audio/clockctl.c $SRC/audio/pcore.c"
fail=0
run() { local name=$1; shift; echo "=== $name"; if ! "$@"; then echo "!!! $name FAILED"; fail=1; fi; }
cc_run() { local t=$1; shift; gcc $W "$@" -o $B/$t && (cd $B && ./$t); }

run "stream packets, source lock, forwarding rule" cc_run u64stream_test -I$SRC/audio audio/u64stream_test.c $SRC/audio/u64stream.c
run "jitter buffer: order, losses, gaps, restarts"  cc_run jitter_test -I$SRC/audio audio/jitter_test.c $AUDIO -lm
run "whole chain, 4 x 10 min simulated (clock control)" cc_run clock_sim -I$SRC/audio audio/clock_sim.c $AUDIO -lm
cp $SRC/net/net.c $B/net_copy.c                            # a copy without the real dhcpserver.h next to it: the fake one is found
run "network: settings, web page, stream, forwarding, devices" cc_run net_test -DU64SCAN_PAR=3 -Ifakes -I$SRC -I$SRC/net net/net_test.c \
    fakes/fake_lwip.c fakes/fake_player.c fakes/fake_ota_apply.c $B/net_copy.c $SRC/net/md5.c $SRC/net/diag.c \
    $SRC/net/ota.c $SRC/net/mdns_lite.c $SRC/net/np_relay.c $SRC/net/u64scan.c $SRC/net/devices.c $SRC/prefs.c $SRC/audio/u64stream.c
echo; [ $fail = 0 ] && echo "ALL HOST TESTS PASSED" || echo "SOME TESTS FAILED"
exit $fail
