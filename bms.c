#include "hw/internal_adc.h"
#include "hw/duart.h"
#include "hw/time.h"
#include "hw/watchdog.h"
#include "state_machines/contactors.h"

#include "hardware/watchdog.h"
#include "pico/stdlib.h"

#include <stdio.h>

#define CRC16_INIT                  ((uint16_t)-1l)
void memcpy_with_crc16(uint8_t *dest, const uint8_t *src, size_t len, uint16_t *crc16);

int main() {
    stdio_usb_init();

    sleep_ms(4000);

    init_adc();
    //230400
    //init_duart(&duart0, 230400, 0, 1, true);
    init_duart(&duart1, 115200, 26, 27, true); //9375000
    init_watchdog();

    contactors_sm_t contactor_sm;
    sm_init((sm_t*)&contactor_sm, "contactors");

    //char str[] = "Lorem. Lorem. Lorem. Lorem.\n";
    uint8_t str[] = {
        'K', 'e', 'e', 'p', 'A', 'l', 'i', 'v',
    };
    // ipsum dolor sit amet, consectetur adipiscing elit. "
      //          "Sed do eiusmod tempor incididunt ut labore et dolore magna aliqua. ";

    int i=0;
    int n=0;
    while(true) {
        //sprintf((char*)&str[0], "%08d", n++);
        if(n>99999999) {
            n = 0;
        }

        if(duart_send_packet(&duart1, (uint8_t *)str, 8)) {
            //printf("Sent successfully\n");
            //putchar('1');
        } else {
            //printf("Send failed\n");
            //putchar('0');
        }
        if(i++ >= 80) {
            //putchar('\n');
            printf("Temp: %d C\n", get_temperature_c_times10());
            i = 0;


        }
        sleep_us(1);

        while(true) {
            uint8_t buf[256];
            size_t len = duart_read_packet(&duart1, buf, sizeof(buf));
            if(len > 0) {
                printf("<%.*s>", len, buf);
            } else {
                break;
            }
        }


        //printf("ADC 12V: raw=%u smoothed=%u\n", adc_samples_raw[0], adc_samples_smoothed[0]);

        update_millis();

        contactor_sm_tick(&contactor_sm);

        watchdog_update();
    }


    while(true) {
        update_millis();
        
        printf("Current millis: %llu\n", current_millis());
        sleep_ms(1000);

    }

    return 0;
}
