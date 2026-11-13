# OnePage Mini firmware (nRF Connect SDK v3.0.2, sysbuild with MCUboot).
cmake_minimum_required(VERSION 3.20.0)

# Custom board and bindings live in this directory.
list(APPEND BOARD_ROOT ${CMAKE_CURRENT_LIST_DIR})
list(APPEND DTS_ROOT ${CMAKE_CURRENT_LIST_DIR})

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(onepage_mini)

# Portable core: plain C, also built by tests/host.
file(GLOB CORE_SOURCES src/core/*.c)
target_sources(app PRIVATE ${CORE_SOURCES})
target_include_directories(app PRIVATE src src/core)

target_sources(app PRIVATE
  src/main.c
  src/app.c
  src/clock.c
  src/power.c
  src/ble/ble_core.c
  src/ble/ancs.c
  src/ble/ams.c
  src/ble/cts.c
  src/ble/op_service.c
  src/ble/nfc_tag.c
  src/drivers/epd_uc81xx.c
  src/drivers/touch_iqs7222a.c
  src/drivers/charger.c
  src/drivers/battery.c
  src/drivers/alerts_hw.c
  src/drivers/button.c
  src/storage/store.c
  src/storage/books.c
  src/ui/gfx.c
  src/ui/screens.c
  src/ui/fonts_generated.c
)
