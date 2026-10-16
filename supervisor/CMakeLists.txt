cmake_minimum_required(VERSION 3.13)

# include the SDK
include(../pico-sdk/pico_sdk_init.cmake)

project(supervisor C CXX)

# initialize the Raspberry Pi Pico SDK
pico_sdk_init()

######

add_executable(supervisor
    isospi/isosnoop.c
    isospi/isospi_scope.c
    supervisor.c
    )

pico_generate_pio_header(supervisor ${CMAKE_CURRENT_LIST_DIR}/isospi/isosnoop.pio)
pico_generate_pio_header(supervisor ${CMAKE_CURRENT_LIST_DIR}/isospi/isospi_scope.pio)

# pull in common dependencies
target_link_libraries(supervisor pico_stdlib pico_multicore hardware_dma hardware_pio)

target_compile_definitions(supervisor PRIVATE
    USBD_MANUFACTURER="S3XY-BMS"
    USBD_PRODUCT="Supervisor"
)

# Boards modified with an external D+ pull-up powered from the host's VBUS (so a
# switched-off host is never back-powered through D+). The chip's own pull-up,
# powered by the BMS, must then stay off. Leave OFF on unmodified boards, or the
# host never sees the device.
option(BMS_USB_EXTERNAL_PULLUP "Board has a VBUS-powered external USB D+ pull-up" OFF)
if(BMS_USB_EXTERNAL_PULLUP)
    add_compile_definitions(BMS_USB_EXTERNAL_PULLUP=1)
endif()

# Boards with USB VBUS wired to GPIO3 (J22 pin 2) through a 3.3k/5.1k divider.
# The chip's D+ pull-up is then only switched on while the host supplies VBUS,
# so a switched-off host is never back-powered through D+. Leave OFF on boards
# without the divider, or USB never connects.
option(BMS_USB_VBUS_SENSE "Board has USB VBUS sensed on GPIO3" OFF)
if(BMS_USB_VBUS_SENSE)
    if(BMS_USB_EXTERNAL_PULLUP)
        message(FATAL_ERROR "BMS_USB_VBUS_SENSE and BMS_USB_EXTERNAL_PULLUP are alternatives")
    endif()
    add_compile_definitions(BMS_USB_VBUS_SENSE=1)
endif()

set(PICO_STDIO_USB_CONNECT_WAIT_TIMEOUT_MS 3000)

# enable usb output, disable uart output
pico_enable_stdio_usb(supervisor 1)
pico_enable_stdio_uart(supervisor 0)

# create map/bin/hex file etc.
pico_add_extra_outputs(supervisor)
