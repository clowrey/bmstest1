#include "pico/flash.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "tusb.h"

#include <stdbool.h>

void core1_entry() {
    flash_safe_execute_core_init();
    while(true) {
        // just sleep for now
        sleep_ms(1000);
    }
}

int main() {
    stdio_usb_init();

#ifdef BMS_USB_EXTERNAL_PULLUP
    // The external D+ pull-up is powered from VBUS; switch the chip's own one
    // (enabled by the USB stack at init) off so it can't back-power the host.
    tud_disconnect();
#endif

    // Give a chance for USB to enumerate before we start printing.
    sleep_ms(1000);

    multicore_launch_core1(core1_entry);

    bms_init();

    while(true) {
        bms_tick();
        synchronize_time();
    }

    return 0;
}
