#pragma once
#define FLASH_SECTOR_SIZE 4096
#define FLASH_PAGE_SIZE 256
void flash_range_erase(uint32_t off, size_t n); void flash_range_program(uint32_t off, const uint8_t *d, size_t n);
