#include "pico/flash.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "tusb.h"

#ifdef BMS_USB_EXTERNAL_PULLUP
#include "hardware/structs/usb.h"

// The external pull-up keeps D+ high through a reboot, so the host never sees
// this device go away and keeps talking to the old one. Drive SE0 for a moment
// so the host registers a disconnect and enumerates again.
static void usb_force_reenumerate(void) {
    const uint32_t ovr = USB_USBPHY_DIRECT_OVERRIDE_TX_DP_OE_OVERRIDE_EN_BITS | USB_USBPHY_DIRECT_OVERRIDE_TX_DM_OE_OVERRIDE_EN_BITS |
                         USB_USBPHY_DIRECT_OVERRIDE_TX_DP_OVERRIDE_EN_BITS | USB_USBPHY_DIRECT_OVERRIDE_TX_DM_OVERRIDE_EN_BITS;
    hw_clear_bits(&usb_hw->phy_direct, USB_USBPHY_DIRECT_TX_DP_BITS | USB_USBPHY_DIRECT_TX_DM_BITS);
    hw_set_bits(&usb_hw->phy_direct, USB_USBPHY_DIRECT_TX_DP_OE_BITS | USB_USBPHY_DIRECT_TX_DM_OE_BITS);
    hw_set_bits(&usb_hw->phy_direct_override, ovr);
    sleep_ms(20);
    hw_clear_bits(&usb_hw->phy_direct_override, ovr);
}
#endif

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
    usb_force_reenumerate();
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
