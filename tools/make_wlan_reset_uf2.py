#!/usr/bin/env python3
"""
wlan_reset.uf2 for the Pico 2 W: clears the stored WLAN / Ultimate / web-page settings (the flash
sector 8 KB below the end of the 4 MB flash). Firmware, volume and source stay untouched.
After copying it onto the Pico (BOOTSEL / button at power-up) the Pico starts with its own access point
"C64-Audio" again, where the web page needs no password. (Same file as the visualizer's: same place.)
"""
import struct, sys
FLASH_SIZE = 4 * 1024 * 1024
ADDR = 0x10000000 + FLASH_SIZE - 2 * 4096
def block(addr, payload, no, total, family, flags=0x2000):
    return struct.pack("<8I", 0x0A324655, 0x9E5D5157, flags, addr, 256, no, total, family) + payload.ljust(476, b"\0") + struct.pack("<I", 0x0AB16F30)
out = block(0x10FFFF00, b"\xEF" * 256, 0, 2, 0xE48BFF57, flags=0xA000)     # RP2350-E10 workaround block, like picotool
out += block(ADDR, b"\xFF" * 256, 0, 1, 0xE48BFF58)                        # erased settings page
dst = sys.argv[1] if len(sys.argv) > 1 else "wlan_reset.uf2"
open(dst, "wb").write(out)
print(dst, "written")
