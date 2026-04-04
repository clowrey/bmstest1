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

set(PICO_STDIO_USB_CONNECT_WAIT_TIMEOUT_MS 3000)

# enable usb output, disable uart output
pico_enable_stdio_usb(supervisor 1)
pico_enable_stdio_uart(supervisor 0)

# create map/bin/hex file etc.
pico_add_extra_outputs(supervisor)
