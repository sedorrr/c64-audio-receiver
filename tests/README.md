# Tests

| Command | What |
|---|---|
| `tests/run_host_tests.sh` | Host tests (PC, gcc): packet parser / source lock / forwarding rule (`audio/u64stream_test.c`), jitter buffer sample by sample (`audio/jitter_test.c`), the whole chain simulated 4 × 10 min with WLAN bursts, losses and clock drift (`audio/clock_sim.c`), network part with fakes of lwIP / cyw43 / flash / player (`net/net_test.c`) |
| `python3 tests/web/page_test.py` | Browser test of the web page (Playwright + Chromium) against a small mock of the Pico |
| `python3 tests/web/backup_test.py` | Browser test of the backup (download and restore) |
| `python3 tests/web/eco.py` | Browser test of the card "Geräte im Netz": way of the sound along the chain, devices, warnings, "Neu suchen", loaded only while the tab is open; titles sent without password |
| `python3 tests/web/wizard.py` | Browser test of the page opened by the visualizer's setup assistant (`#setup=<json>`): filled in after loading, marks, banner, link cleaned; save sends forwarding, `src` and `play`; new row, full list, broken link |
| `python3 tests/web/help.py` | Browser test of the "Hilfe" tab: manual from `/hilfe` via a real HTTP server with gzip (as `help_page.h` holds it), loaded once, context links of every tab, contents, links, phone width |

The fakes in `host/fakes/` replace the Pico SDK and lwIP headers; `net.c` is compiled from a copy so the fake
`dhcpserver.h` is found before the real one. Not covered by host tests (needs the hardware): `i2s_out.c`, `player.c`, `main.c`.
