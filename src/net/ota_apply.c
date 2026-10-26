/*
 * Final step of the web firmware update: copy the staged image over the running firmware.
 * Everything here runs from RAM with interrupts off - while it works, the flash no longer holds the
 * old program. Sector 0 (with the IMAGE_DEF block the boot ROM looks for) is erased first and written
 * last: if power fails in between, the boot ROM finds no image and starts in USB drive mode, where the
 * .uf2 can be copied as usual. A hang is caught by the watchdog with the same result.
 */
#include "pico/stdlib.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "hardware/watchdog.h"
#include "hardware/structs/watchdog.h"
#include "hardware/regs/addressmap.h"
#include "ota.h"

#define WD_LOAD (8000u * 1000u)                           /* 8 s per sector is far more than an erase takes */

static void __no_inline_not_in_flash_func(copy_sector)(uint32_t src_off, uint32_t dst_off, uint8_t *buf) {
    /* volatile word copy: a plain loop could become a memcpy call, and memcpy lives in flash */
    const volatile uint32_t *src = (const volatile uint32_t *)(XIP_BASE + src_off);
    volatile uint32_t *dst = (volatile uint32_t *)buf;
    for (int i = 0; i < FLASH_SECTOR_SIZE / 4; i++) dst[i] = src[i];
    watchdog_hw->load = WD_LOAD;
    flash_range_erase(dst_off, FLASH_SECTOR_SIZE);
    flash_range_program(dst_off, buf, FLASH_SECTOR_SIZE);
}

void __no_inline_not_in_flash_func(ota_apply_now)(uint32_t staged_offset, uint32_t size, uint8_t *buf) {
    uint32_t sectors = (size + FLASH_SECTOR_SIZE - 1) / FLASH_SECTOR_SIZE;
    (void)save_and_disable_interrupts();
    watchdog_hw->load = WD_LOAD;
    hw_set_bits(&watchdog_hw->ctrl, WATCHDOG_CTRL_ENABLE_BITS);
    flash_range_erase(0, FLASH_SECTOR_SIZE);              /* from now on: no bootable image until the end */
    for (uint32_t s = 1; s < sectors; s++) copy_sector(staged_offset + s * FLASH_SECTOR_SIZE, s * FLASH_SECTOR_SIZE, buf);
    copy_sector(staged_offset, 0, buf);
    watchdog_hw->scratch[4] = 0;                          /* plain boot, no watchdog boot vector */
    hw_set_bits(&watchdog_hw->ctrl, WATCHDOG_CTRL_TRIGGER_BITS);
    while (1) { }
}
