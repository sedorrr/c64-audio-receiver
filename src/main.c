/*
 * C64 audio receiver - Raspberry Pi Pico 2 W + PCM5102A
 * Receives the Ultimate 64's audio stream over WLAN and plays it through an I2S DAC into the line input
 * of a stereo; forwards the stream to the C64 visualizer.
 *
 * Main loop: network (net.c), button, on-board LED. The audio itself runs in a timer interrupt (player.c).
 *
 * Optional button (GP15 to GND): short = play / stop, long (1 s) = next Ultimate,
 * held while plugging in = USB drive mode (like BOOTSEL).
 */
#include "pico/stdlib.h"
#include "pico/bootrom.h"
#include "pico/cyw43_arch.h"
#include "hardware/watchdog.h"
#include "config.h"
#include "player.h"
#include "net/net.h"
#include "net/diag.h"

static void bootsel_check(void) {
    gpio_init(PIN_BUTTON);
    gpio_set_dir(PIN_BUTTON, GPIO_IN);
    gpio_pull_up(PIN_BUTTON);
    sleep_ms(30);                                         /* let the pull-up settle */
    for (int i = 0; i < 10; i++) {                        /* must be held for 100 ms */
        if (gpio_get(PIN_BUTTON)) return;
        sleep_ms(10);
    }
    reset_usb_boot(0, 0);                                 /* never returns */
}

/* button: debounced; short press on release, long press after 1 s */
static void button_task(void) {
    static bool down = false, long_done = false;
    static uint32_t since = 0, stable_at = 0;
    static bool raw_prev = false;
    uint32_t now = to_ms_since_boot(get_absolute_time());
    bool raw = !gpio_get(PIN_BUTTON);
    if (raw != raw_prev) { raw_prev = raw; stable_at = now; return; }
    if (now - stable_at < 30 || raw == down) {
        if (down && !long_done && now - since >= 1000) { long_done = true; net_next_source(); }
        return;
    }
    down = raw;
    if (down) { since = now; long_done = false; }
    else if (!long_done) net_toggle_play();
}

/* on-board LED (on the WLAN chip): blinks while connecting, double blink = own access point,
   short flash every 2 s = ready, on = playing */
static void led_task(void) {
    static int last = -1;
    uint32_t t = to_ms_since_boot(get_absolute_time());
    bool on;
    int mode = net_led();
    if (mode == NET_LED_OFF) return;
    switch (mode) {
        case NET_LED_CONNECTING: on = (t / 250) & 1; break;
        case NET_LED_AP:         on = (t % 1000) < 100 || ((t % 1000) >= 200 && (t % 1000) < 300); break;
        case NET_LED_PLAYING:    on = true; break;
        default:                 on = (t % 2000) < 60; break;
    }
    if (on != last) { cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, on); last = on; }
}

int main(void) {
    bootsel_check();
#ifdef DEBUG_USB
    stdio_init_all();
#endif
    DIAG("start: C64-Audioempfänger, Firmware %s", __DATE__ " " __TIME__);
    net_load_settings();
    player_init(net_latency_ms(), net_volume());
    net_init();
    watchdog_enable(5000, true);
    while (true) {
        net_task();
        button_task();
        led_task();
        watchdog_update();
        sleep_ms(2);
    }
}
