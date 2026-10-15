#include "pins.h"

#include "hardware/watchdog.h"
#include "pico/stdlib.h"
#include "tusb.h"

#ifdef BMS_USB_EXTERNAL_PULLUP
#include "hardware/structs/usb.h"

// The external pull-up keeps D+ high through a reboot, so the host never sees
// this device go away and keeps talking to the old one. Drive SE0 for a moment
// so the host registers a disconnect and enumerates again.
static void usb_force_reenumerate(void) {
    // SIE direct bus drive: DP and DM both low = SE0
    hw_clear_bits(&usb_hw->sie_ctrl, USB_SIE_CTRL_DIRECT_DP_BITS | USB_SIE_CTRL_DIRECT_DM_BITS);
    hw_set_bits(&usb_hw->sie_ctrl, USB_SIE_CTRL_DIRECT_EN_BITS);
    sleep_ms(50);
    hw_clear_bits(&usb_hw->sie_ctrl, USB_SIE_CTRL_DIRECT_EN_BITS);
}
#endif

#ifdef BMS_USB_VBUS_SENSE
// USB VBUS reaches PIN_USB_VBUS_SENSE through a 3.3k/5.1k divider. The chip's
// D+ pull-up is only switched on while the host supplies VBUS, so a switched-off
// host is never back-powered through D+. A reboot drops the pull-up with the
// USB controller, so the host also sees every reboot.
#define USB_VBUS_DEBOUNCE_US 50000

static bool usb_vbus_connected;
static bool usb_vbus_pending;
static absolute_time_t usb_vbus_changed_at;

static void usb_vbus_sense_init(void) {
    gpio_init(PIN_USB_VBUS_SENSE);
    gpio_set_dir(PIN_USB_VBUS_SENSE, GPIO_IN);
    gpio_disable_pulls(PIN_USB_VBUS_SENSE);
    sleep_us(100);

    // The USB stack switches the pull-up on at init
    usb_vbus_connected = gpio_get(PIN_USB_VBUS_SENSE);
    if(!usb_vbus_connected) {
        tud_disconnect();
    }
}

static void usb_vbus_sense_poll(void) {
    bool vbus = gpio_get(PIN_USB_VBUS_SENSE);
    if(vbus == usb_vbus_connected) {
        usb_vbus_pending = false;
        return;
    }
    if(!usb_vbus_pending) {
        usb_vbus_pending = true;
        usb_vbus_changed_at = get_absolute_time();
        return;
    }
    if(absolute_time_diff_us(usb_vbus_changed_at, get_absolute_time()) < USB_VBUS_DEBOUNCE_US) {
        return;
    }

    usb_vbus_pending = false;
    usb_vbus_connected = vbus;
    if(vbus) {
        tud_connect();
    } else {
        tud_disconnect();
    }
}
#endif

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

#ifdef BMS_USB_EXTERNAL_PULLUP
    // The external D+ pull-up is powered from VBUS; switch the chip's own one
    // (enabled by the USB stack at init) off so it can't back-power the host.
    tud_disconnect();
    usb_force_reenumerate();
#endif
#ifdef BMS_USB_VBUS_SENSE
    usb_vbus_sense_init();
#endif

    sleep_ms(4000);

    init_watchdog();
    printf("Supervisor starting...\n");

    isosnoop_setup(
        PIN_ISOSPI_RX_HIGH,
        PIN_ISOSNOOP_RX_DEBUG,
        PIN_ISOSPI_RX_AND,
        PIN_ISOSNOOP_TIMER_DISABLE
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
#ifdef BMS_USB_VBUS_SENSE
        usb_vbus_sense_poll();
#endif

        sleep_ms(30);
    }

    return 0;
}
