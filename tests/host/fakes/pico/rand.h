#pragma once
#include <stdint.h>
#include <stdlib.h>
static inline uint32_t get_rand_32(void){ return (uint32_t)rand() * 2654435761u; }
