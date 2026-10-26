/* Search the home network (/24 of the Pico's own address) for Ultimate devices: GET /v1/info on port 80 of every
   address, a few at a time; an answer whose "product" contains "Ultimate" counts (Ultimate 64 / Elite / II+,
   C64 Ultimate). Started from the web page, runs in the main loop (~30 s).
   Same file as in the C64 visualizer (../Pico2_c64_visualizer) - change both. */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#define U64SCAN_MAX_FOUND 8
void u64scan_start(uint32_t my_ip);                   /* lwIP byte order (a | b<<8 | c<<16 | d<<24) */
void u64scan_task(void);                              /* main loop */
bool u64scan_running(void);
int  u64scan_json(char *out, int n);                  /* {"running":..,"done":..,"total":..,"found":[{"ip","name","product"}]} */
