#include "pins.h"

#include "hardware/watchdog.h"
#include "pico/stdlib.h"

#include <stdio.h>

#define MCU_CHECK_PIN 0

#define INTERNAL_UART uart0
#define INTERNAL_UART_BAUDRATE 115200
#define INTERNAL_UART_RX_PIN 1
#define INTERNAL_UART_TX_PIN 2

#define HMI_UART uart1
#define HMI_UART_BAUDRATE 460800
#define HMI_UART_TX_PIN 38
#define HMI_UART_RX_PIN 39

#define OUTPUT_ENABLE_PIN 14
#define LED_PIN 25

const uint8_t keepalive_msg[] = {
    0xff, 0x07,
    'K', 'e', 'e', 'p', 'A', 'l', 'i', 'v',
    0x20, 0xe8
};

// supervisor serial: 675DE5D4CCFD90CA

int rx_pointer = 0;

void init_serial() {
    uart_init(INTERNAL_UART, INTERNAL_UART_BAUDRATE);
    gpio_set_function(INTERNAL_UART_TX_PIN, UART_FUNCSEL_NUM(INTERNAL_UART, INTERNAL_UART_TX_PIN));
    gpio_set_function(INTERNAL_UART_RX_PIN, UART_FUNCSEL_NUM(INTERNAL_UART, INTERNAL_UART_RX_PIN));
    uart_set_hw_flow(INTERNAL_UART, false, false);
    uart_set_format(INTERNAL_UART, 8, 1, UART_PARITY_NONE);
    uart_set_fifo_enabled(INTERNAL_UART, true);

    gpio_init(HMI_UART_TX_PIN);
    gpio_init(HMI_UART_RX_PIN);
    gpio_set_dir(HMI_UART_TX_PIN, GPIO_IN);
    gpio_set_dir(HMI_UART_RX_PIN, GPIO_IN);

    // uart_init(HMI_UART, HMI_UART_BAUDRATE);
    // //gpio_set_function(HMI_UART_TX_PIN, UART_FUNCSEL_NUM(HMI_UART, HMI_UART_TX_PIN));
    // gpio_pull_up(HMI_UART_TX_PIN);
    // gpio_set_function(HMI_UART_RX_PIN, UART_FUNCSEL_NUM(HMI_UART, HMI_UART_RX_PIN));
    // uart_set_hw_flow(HMI_UART, false, false);
    // uart_set_format(HMI_UART, 8, 1, UART_PARITY_NONE);
    // uart_set_fifo_enabled(HMI_UART, false);
}

void enable_hmi_tx() {
    gpio_set_function(HMI_UART_TX_PIN, UART_FUNCSEL_NUM(HMI_UART, HMI_UART_TX_PIN));
}

void disable_hmi_tx() {
    gpio_set_function(HMI_UART_TX_PIN, GPIO_FUNC_SIO);
}

void internal_send_blocking(const uint8_t *data, size_t len) {
    uart_write_blocking(INTERNAL_UART, data, len);
}

void init_watchdog() {
    if (watchdog_enable_caused_reboot()) {
        printf("Rebooted by Watchdog!\n");
    } else {
        printf("Clean boot\n");
    }

    watchdog_enable(5000, 0); // second arg enables pause on debug
}

void supervisor_tick() {
    while(uart_is_readable(INTERNAL_UART)) {
        uint8_t c = uart_getc(INTERNAL_UART);
        if(c == keepalive_msg[rx_pointer]) {
            rx_pointer++;
            if(rx_pointer >= sizeof(keepalive_msg)) {
                // Full message received
                rx_pointer = 0;
                
                gpio_put(OUTPUT_ENABLE_PIN, true);
                printf("Got keepalive\n");
                watchdog_update();

                // toggle LED state
                gpio_put(LED_PIN, !gpio_get(LED_PIN));
            }
        } else {
            rx_pointer = 0;
        }
    }
}


int main() {
    stdio_usb_init();

    sleep_ms(4000);

    init_watchdog();
    printf("Supervisor starting...\n");

    isosnoop_setup(
        PIN_ISOSPI_RX_HIGH,
        PIN_ISOSNOOP_RX_DEBUG,
        PIN_ISOSPI_RX_AND,
        PIN_ISOSNOOP_TIMER_DISABLE
    );
    isospi_scope_setup(
        PIN_ISOSPI_RX_HIGH,
        1
    );

    gpio_init(MCU_CHECK_PIN);
    gpio_set_dir(MCU_CHECK_PIN, GPIO_IN);
    bool mcu_check = gpio_get(MCU_CHECK_PIN);
    if(!mcu_check) {
        printf("Error: Supervisor firmware is running on the main MCU!\n");
        while(1);
    }

    init_serial();
    gpio_init(OUTPUT_ENABLE_PIN);
    gpio_init(LED_PIN);
    gpio_set_dir(OUTPUT_ENABLE_PIN, GPIO_OUT);
    gpio_set_dir(LED_PIN, GPIO_OUT);
    gpio_put(OUTPUT_ENABLE_PIN, false);
    gpio_put(LED_PIN, true);

    sleep_ms(2000);

    while(1) {
        supervisor_tick();

        isosnoop_print_buffer();
        //print_isospi_scope_output();

        sleep_ms(30);
    }

    return 0;
}
