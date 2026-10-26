/*
 * Devices in the network: every C64 device (visualizer, audio receiver) says every 10 s by UDP broadcast who it is
 * and how the stream flows through it; a query makes all of them answer at once. Port and format: docs/HANDBUCH.md
 * (Anhang "Netzwerk-Protokolle"). Only in the WLAN firmware.
 */
#pragma once
#include <stdbool.h>

#define DEV_PORT 11064                      /* UDP: "C64DEV1 {json}" = beacon, "C64DEV1?" = query */

void dev_start(void);                       /* in the home network, with the servers (lwIP lock taken inside) */
void dev_task(void);                        /* main loop: beacons, answers to queries, forgetting silent devices */
void dev_query(void);                       /* ask all devices to answer now (web page "Neu suchen") */
int  dev_json(char *out, int n);            /* {"list":["{..}",..]} - this device first, every entry as a JSON string */

/* the beacon of this device (net.c): one line of JSON, no newline */
int  net_self_json(char *out, int n);
