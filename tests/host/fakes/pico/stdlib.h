#pragma once
#include <stdint.h>
#include <stdbool.h>
typedef int64_t absolute_time_t;
extern int64_t FAKE_NOW;
static inline absolute_time_t get_absolute_time(void){return FAKE_NOW;}
static inline absolute_time_t make_timeout_time_ms(uint32_t ms){return FAKE_NOW+ms*1000LL;}
static inline int64_t absolute_time_diff_us(absolute_time_t a, absolute_time_t b){return b-a;}
static inline absolute_time_t delayed_by_ms(absolute_time_t t, uint32_t ms){return t+ms*1000LL;}
static inline bool time_reached(absolute_time_t t){return FAKE_NOW>=t;}
static inline uint32_t to_ms_since_boot(absolute_time_t t){return (uint32_t)(t/1000);}
static inline uint32_t time_us_32(void){return (uint32_t)FAKE_NOW;}
static inline uint64_t time_us_64(void){return (uint64_t)FAKE_NOW;}
static inline void tight_loop_contents(void){}
#define PICO_FLASH_SIZE_BYTES (4*1024*1024)
