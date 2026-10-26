/* fake of ota_apply.c: the real one overwrites the running firmware and resets (RAM code, registers) */
#include <stdint.h>
uint32_t OTA_APPLIED_OFF = 0, OTA_APPLIED_SIZE = 0;
void ota_apply_now(uint32_t staged_offset, uint32_t size, uint8_t *ram_buf_4k) {
    (void)ram_buf_4k; OTA_APPLIED_OFF = staged_offset; OTA_APPLIED_SIZE = size;
}
