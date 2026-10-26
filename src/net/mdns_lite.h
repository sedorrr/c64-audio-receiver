/* Minimal mDNS responder for "<name>.local" (default MDNSL_DEFAULT_NAME, "c64viz" unless the build sets it; replaces lwIP's, which leaves AAAA questions
   unanswered - macOS then waits 5 s on every lookup). Answers A questions with the address and NSEC ("only A
   exists"), AAAA and other types with NSEC alone. At the start it first asks whether the name is taken (3 probes,
   250 ms apart); if another device answers, it takes "<name>-2", "-3" ... (several visualizers in one network). */
#pragma once
#include <stdbool.h>
#include <stdint.h>
struct netif;
#define MDNSL_NAME_MAX 16                             /* configurable base name; the suffix adds up to 3 */
bool mdnsl_name_valid(const char *s);                 /* 1..16 of a-z 0-9 -, not starting/ending with - */
void mdnsl_set_name(const char *base);                /* before mdnsl_start; invalid or empty -> the default */
const char *mdnsl_name(void);                         /* the name in use (may carry a suffix) */
bool mdnsl_ready(void);                               /* probing done, the name is ours */
void mdnsl_start(struct netif *n);                    /* join 224.0.0.251, listen on 5353, start probing */
void mdnsl_task(void);                                /* main loop, inside cyw43_arch_lwip_begin/end: probe timers */
/* builds the response to one mDNS query: length, or 0 if it is not for us (pure function, host-tested) */
int  mdnsl_answer(const uint8_t *q, int qlen, const uint8_t ip[4], bool legacy, uint8_t *out, int max);
/* is this message an answer from another device that holds our name (A record, other address)? (pure) */
bool mdnsl_conflict(const uint8_t *m, int len, const uint8_t ip[4]);
