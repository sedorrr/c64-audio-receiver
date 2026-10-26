cmake_minimum_required(VERSION 3.13)

# Board and platform MUST be set before including the SDK, otherwise you silently get an RP2040 build.
# The receiver needs WLAN: Pico 2 W only.
set(PICO_BOARD pico2_w CACHE STRING "Board type")
set(PICO_PLATFORM rp2350-arm-s CACHE STRING "Platform")

include(pico_sdk_import.cmake)

project(c64_audio C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

option(DEBUG_USB "Diagnostics on USB serial" OFF)

if(NOT PICO_CYW43_SUPPORTED)
    message(FATAL_ERROR "The audio receiver needs a Pico 2 W (cmake -DPICO_BOARD=pico2_w)")
endif()

add_executable(c64audio
    src/main.c
    src/player.c
    src/prefs.c
    src/i2s_out.c
    src/audio/u64stream.c
    src/audio/jitter.c
    src/audio/clockctl.c
    src/audio/pcore.c
    src/net/net.c
    src/net/dhcpserver.c
    src/net/md5.c
    src/net/diag.c
    src/net/ota.c
    src/net/ota_apply.c
    src/net/mdns_lite.c
    src/net/np_relay.c
    src/net/u64scan.c
    src/net/devices.c
)
pico_generate_pio_header(c64audio ${CMAKE_CURRENT_LIST_DIR}/src/i2s.pio)
target_include_directories(c64audio PRIVATE ${CMAKE_CURRENT_LIST_DIR}/src ${CMAKE_CURRENT_LIST_DIR}/src/net)
target_compile_definitions(c64audio PRIVATE CYW43_HOST_NAME="c64audio" MDNSL_DEFAULT_NAME="c64audio"
                           U64SCAN_PAR=3)   # network search: 3 addresses at a time (6 cost audio packets, see CLAUDE.md)
target_link_libraries(c64audio pico_stdlib pico_cyw43_arch_lwip_threadsafe_background pico_rand pico_bootrom
                      hardware_pio hardware_dma hardware_clocks hardware_flash hardware_sync hardware_watchdog hardware_irq)
if(DEBUG_USB)
    pico_enable_stdio_usb(c64audio 1)
    target_compile_definitions(c64audio PRIVATE DEBUG_USB)
else()
    pico_enable_stdio_usb(c64audio 0)
endif()
pico_enable_stdio_uart(c64audio 0)
pico_add_extra_outputs(c64audio)
