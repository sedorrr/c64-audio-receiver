/* Diagnostics ring buffer - see diag.h. */
#include <stdio.h>
#include <stdarg.h>
#include "pico/stdlib.h"
#include "hardware/sync.h"
#include "diag.h"

#define DIAG_RING 12288                                   /* a full FTP browse logs ~3 KB */
static char d_ring[DIAG_RING];
static volatile uint32_t d_head = 0, d_tail = 0;         /* free-running, used modulo DIAG_RING */
#ifdef DEBUG_USB
static volatile uint32_t d_usb = 0;                       /* next byte to print over USB */
#endif

/* any context: format the line and put it into the ring (the oldest lines make room) */
void diag_printf(const char *fmt, ...) {
    char line[240];
    uint32_t ms = to_ms_since_boot(get_absolute_time());
    int n = snprintf(line, sizeof line, "[%7lu.%03lu] ", (unsigned long)(ms / 1000), (unsigned long)(ms % 1000));
    va_list ap;
    va_start(ap, fmt);
    n += vsnprintf(line + n, sizeof line - (size_t)n - 1, fmt, ap);
    va_end(ap);
    if (n > (int)sizeof line - 2) n = (int)sizeof line - 2;
    line[n++] = '\n';
    uint32_t irq = save_and_disable_interrupts();
    uint32_t h = d_head;
    if (DIAG_RING - (h - d_tail) < (uint32_t)n) d_tail = h + (uint32_t)n - DIAG_RING;   /* overwrite the oldest */
    for (int i = 0; i < n; i++) d_ring[(h + (uint32_t)i) % DIAG_RING] = line[i];
    d_head = h + (uint32_t)n;
#ifdef DEBUG_USB
    if (d_head - d_usb > DIAG_RING) d_usb = d_head - DIAG_RING;                          /* USB fell behind */
#endif
    restore_interrupts(irq);
}

/* copy the buffered lines for GET /diag (oldest first; a cut-off first line is harmless); any context */
int diag_snapshot(char *dst, int max) {
    uint32_t irq = save_and_disable_interrupts();
    uint32_t h = d_head, t = d_tail;
    if (h - t > (uint32_t)max) t = h - (uint32_t)max;
    int n = 0;
    while (t != h && n < max) dst[n++] = d_ring[t++ % DIAG_RING];
    restore_interrupts(irq);
    return n;
}

#ifdef DEBUG_USB
/* main loop: print new lines over USB (stdio_init_all() runs in main()) */
void diag_task(void) {
    char buf[512];
    int n = 0;
    uint32_t irq = save_and_disable_interrupts();
    uint32_t u = d_usb;
    if (d_head - u > DIAG_RING) u = d_head - DIAG_RING;
    while (u != d_head && n < (int)sizeof buf) buf[n++] = d_ring[u++ % DIAG_RING];
    d_usb = u;
    restore_interrupts(irq);
    if (n) fwrite(buf, 1, (size_t)n, stdout);
}
#endif
