/*
 * "Now playing" relay: the Ultimate64 SID Jukebox on the PC sends its title messages (POST /jb/np?...) to the one
 * Pico entered there. When that is this receiver, it passes every message on unchanged, in order, to the visualizer
 * (same target as the stream forwarding), logging in there with the visualizer's web password (HTTP Digest).
 * The visualizer answers 409 while it has not taken over the previous message: then it is sent again shortly.
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>

#define NPR_QUEUE 6                              /* messages waiting (the PC sends ~1 per tune + one every 30 s) */

void npr_queue(const char *path);                /* any context: "/jb/np?..." as received */
/* main loop, inside the lwIP lock: send the next message to ip (0 = no target yet), host for the Host header */
void npr_task(uint32_t ip, const char *host, const char *password);
const char *npr_state(void);                     /* for the status page, "" = nothing relayed yet */
uint32_t npr_sent(void);                         /* messages the visualizer took */
