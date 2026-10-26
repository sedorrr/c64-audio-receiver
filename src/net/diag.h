/*
 * Diagnostics ring buffer (always on): net/FTP/jukebox events are written from any context
 * (lwIP callbacks run in IRQ context, so they must not touch stdio) and kept in a ring that
 * overwrites its oldest lines when full. The web page reads it via GET /diag (diag_snapshot).
 * With -DDEBUG_USB=ON the main loop additionally prints new lines over USB CDC (diag_task);
 * read on the PC with e.g.:  screen /dev/tty.usbmodem* 115200
 */
#pragma once
void diag_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
int  diag_snapshot(char *dst, int max);                   /* copy the buffered lines, oldest first */
#define DIAG(...) diag_printf(__VA_ARGS__)
#ifdef DEBUG_USB
void diag_task(void);                                     /* main loop: flush new lines to USB */
#else
static inline void diag_task(void) {}
#endif
