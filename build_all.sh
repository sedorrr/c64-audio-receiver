#!/bin/bash
# Builds the firmware into firmware/: normal + debug (diagnostics on USB serial). Pico 2 W only (needs WLAN).
# Needs: PICO_SDK_PATH (Pico SDK 2.1.1 with submodules lib/cyw43-driver and lib/lwip), cmake, arm-none-eabi-gcc.
set -e
cd "$(dirname "$0")"
: "${PICO_SDK_PATH:?PICO_SDK_PATH is not set}"
JOBS=${JOBS:-8}
python3 tools/make_web_page.py                      # src/net/web_page.h from tools/web/index.html
build() {   # $1 = name, $2.. = cmake options
  local d=build/$1; shift
  rm -rf "$d"; mkdir -p "$d"
  (cd "$d" && cmake ../.. -DPICO_BOARD=pico2_w "$@" >/dev/null && make -j$JOBS >/dev/null)
  echo "$d"
}
mkdir -p firmware
d=$(build release); cp $d/c64audio.uf2 firmware/c64audio.uf2; echo "Pico 2 W  c64audio.uf2"
d=$(build debug -DDEBUG_USB=ON); cp $d/c64audio.uf2 firmware/c64audio_debug.uf2; echo "Pico 2 W  c64audio_debug.uf2"
python3 tools/make_wlan_reset_uf2.py firmware/wlan_reset.uf2 >/dev/null
arm-none-eabi-size build/release/c64audio.elf | tail -1 | awk '{printf "flash %d KB, RAM (data+bss) %d KB\n", ($1+$2)/1024, ($2+$3)/1024}'
echo "done - firmware/ is up to date"
