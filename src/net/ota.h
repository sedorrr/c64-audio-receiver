/* Firmware update over the web page (Pico 2 W): the browser converts the .uf2 into a flat image and
   sends it in chunks; the Pico stages it in free flash, checks it, then copies it over itself. */
#pragma once
#include <stdbool.h>
#include <stdint.h>
void ota_task(void);                                  /* main loop (from net_task): flash writes, check, apply */
bool ota_busy_body(void);                             /* the shared request body buffer is still needed */
bool ota_active(void);                                /* an update is being received / checked / applied: no playback */
const char *ota_build(void);                          /* build stamp of the running firmware */
/* web routes /fw/...: HTTP status, or 0 if the path is not ours */
int  ota_route(bool post, const char *path, const char *body, int len, const char **out, int *outlen, const char **type);
/* copy the staged image over the running firmware and reset; never returns (ota_apply.c, runs from RAM) */
void ota_apply_now(uint32_t staged_offset, uint32_t size, uint8_t *ram_buf_4k);
