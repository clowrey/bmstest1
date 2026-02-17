#include "ads1115.h"
#include "config/allocations.h"
#include "config/pins.h"
#include "app/model.h"
#include "drivers/chip/i2c.h"
#include "sys/logging/logging.h"
#include "lib/aema.h"

#include "hardware/irq.h"
#include "hardware/i2c.h"
#include "hardware/gpio.h"
#include "pico/stdlib.h"
#include <stdio.h>

// With floating inputs, higher sample rates lead to larger readings - eg, 350mV
// at 128SPS, but 1000mV at 250SPS, it is not clear why.

// How fast to sample (data rate setting)
static const int ADS1115_SAMPLE_RATE_SETTING = ADS1115_CONFIG_DR_128SPS;
// How long each conversion takes
static const int ADS1115_CONVERSION_TIME_MS = 8;
// How long extra to wait to be safe before reading conversion result
static const int ADS1115_CONVERSION_TIME_EXTRA_MS = 1;
// How often we do a full sampling cycle
static const int ADS1115_SAMPLING_PERIOD = (ADS1115_CONVERSION_TIME_MS + ADS1115_CONVERSION_TIME_EXTRA_MS) * ADS1115_CHANNEL_COUNT + 10;
// I2C timeout for blocking calls
static const uint32_t ADS1115_I2C_TIMEOUT_US = 10000;

// We are oversampling by 8, we effectively sample every 200ms +/- some jitter

static void ads1115_i2c_callback(i2c_inst_t *i2c, bool success, void *user_data);
static int64_t ads1115_conversion_timer_callback(alarm_id_t id, void *user_data);
static bool ads1115_periodic_timer_callback(struct repeating_timer *t);

sampler_t samples[ADS1115_CHANNEL_COUNT] = {0};
float filtered_samples[ADS1115_CHANNEL_COUNT] = { [0 ... (ADS1115_CHANNEL_COUNT-1)] = NAN };
float sample_deviations[ADS1115_CHANNEL_COUNT] = { [0 ... (ADS1115_CHANNEL_COUNT-1)] = NAN };

bool ads1115_init(ads1115_t *dev, uint8_t addr) {
    dev->i2c = ADS1115_I2C; // Based on pins 18, 19
    dev->addr = addr;
    dev->busy = false;
    dev->state = ADS1115_STATE_IDLE;
    dev->current_channel = 0;

    // Initialize I2C using new async-capable driver
    i2c_async_init(dev->i2c, 400 * 1000, PIN_ADS1115_I2C_SDA, PIN_ADS1115_I2C_SCL);

    // Test read to check if chip is present (blocking is fine for init)
    uint8_t test_buf[2];
    if (!i2c_blocking_read_reg(dev->i2c, dev->addr, ADS1115_REG_CONFIG, test_buf, 2, ADS1115_I2C_TIMEOUT_US)) {
        return false;
    }

    // Start periodic sampling
    static struct repeating_timer timer;
    add_repeating_timer_ms(ADS1115_SAMPLING_PERIOD, ads1115_periodic_timer_callback, dev, &timer);

    return true;
}

static void ads1115_start_conversion(ads1115_t *dev, int channel) {
    uint16_t config = ADS1115_CONFIG_OS_SINGLE | 
                      ADS1115_CONFIG_MODE_SINGLE | 
                      ADS1115_SAMPLE_RATE_SETTING | 
                      ADS1115_CONFIG_COMP_QUE_NONE;
    
    switch (channel) {
        case 0: 
            // ADC0 - ADC1 (battery voltage)
            config |= ADS1115_CONFIG_MUX_DIFF_0_1
                      | ADS1115_CONFIG_PGA_1_024V;
            break;
        case 1: 
            // ADC2 - ADC3 (output voltage)
            config |= ADS1115_CONFIG_MUX_DIFF_2_3 
                      | ADS1115_CONFIG_PGA_1_024V;
            break;
        case 2: 
            // ADC1 - ADC3 (voltage across negative contactor)
            config |= ADS1115_CONFIG_MUX_DIFF_1_3 
                      | ADS1115_CONFIG_PGA_1_024V;
            break;
        case 3: 
            // ADC0 - ADC3 (voltage between battery positive and negative output)
            config |= ADS1115_CONFIG_MUX_DIFF_0_3
                      | ADS1115_CONFIG_PGA_1_024V;
            break;

        case 4:
            // ADC0 - GND (battery pos)
            config |= ADS1115_CONFIG_MUX_SINGLE_0
                      | ADS1115_CONFIG_PGA_4_096V;
            break;
        case 5:
            // ADC1 - GND (battery neg)
            config |= ADS1115_CONFIG_MUX_SINGLE_1
                      | ADS1115_CONFIG_PGA_4_096V;
            break;
    }

    uint8_t buf[2];
    buf[0] = (config >> 8) & 0xFF;
    buf[1] = config & 0xFF;

    dev->state = ADS1115_STATE_WAIT_CONVERSION;
    i2c_async_write_reg(dev->i2c, dev->addr, ADS1115_REG_CONFIG, buf, 2, ads1115_i2c_callback, dev);
}

static void ads1115_start_sampling(ads1115_t *dev) {
    if (dev->busy) return;
    dev->busy = true;
    dev->current_channel = 0;
    ads1115_start_conversion(dev, 0);
}

static void ads1115_i2c_callback(i2c_inst_t *i2c, bool success, void *user_data) {
    ads1115_t *dev = (ads1115_t *)user_data;
    (void)i2c;

    if (!success) {
        dev->busy = false;
        dev->state = ADS1115_STATE_IDLE;
        return;
    }

    if (dev->state == ADS1115_STATE_WAIT_CONVERSION) {
        // Config written, now wait for conversion to complete
        add_alarm_in_ms(
            ADS1115_CONVERSION_TIME_MS + ADS1115_CONVERSION_TIME_EXTRA_MS,
            ads1115_conversion_timer_callback, 
            dev, 
            true
        );
    } else if (dev->state == ADS1115_STATE_READING_CONVERSION) {
        // Conversion data read complete
        const int16_t sample = (int16_t)((dev->async_buf[0] << 8) | dev->async_buf[1]);

        // Raw res is 13mV per bit?
        
        sampler_add(&samples[dev->current_channel], (int32_t)sample, ADS1115_OVERSAMPLING, 0);

        aema_update(
            &filtered_samples[dev->current_channel], 
            &sample_deviations[dev->current_channel],
            (float)sample, 
            0.001f, 0.25f, 15.0f, 150.0f
        );

        if (dev->cal_samples_left[dev->current_channel] > 0) {
            dev->cal_accumulator[dev->current_channel] += sample;
            dev->cal_samples_left[dev->current_channel]--;
            if(dev->current_channel==0) {
                debug_printf("Cal remaining: %d\n", dev->cal_samples_left[0]);
            }
        }
        
        dev->current_channel++;
        if (dev->current_channel < ADS1115_CHANNEL_COUNT) {
            ads1115_start_conversion(dev, dev->current_channel);
        } else {
            dev->busy = false;
            dev->state = ADS1115_STATE_IDLE;
        }
    }
}

static int64_t ads1115_conversion_timer_callback(alarm_id_t id, void *user_data) {
    (void)id;
    ads1115_t *dev = (ads1115_t *)user_data;
    dev->state = ADS1115_STATE_READING_CONVERSION;
    i2c_async_read_reg(dev->i2c, dev->addr, ADS1115_REG_CONVERSION, dev->async_buf, 2, ads1115_i2c_callback, dev);
    return 0;
}

static bool ads1115_periodic_timer_callback(struct repeating_timer *t) {
    ads1115_t *dev = (ads1115_t *)t->user_data;
    ads1115_start_sampling(dev);
    return true;
}

void ads1115_start_calibration(ads1115_t *dev, uint16_t num_samples) {
    for (int ch = 0; ch < 4; ch++) {
        dev->cal_accumulator[ch] = 0;
        debug_printf("Starting calibration for channel %d, num_samples=%d\n", ch, num_samples);
        dev->cal_samples_left[ch] = num_samples;
    }
}

bool ads1115_calibration_finished(ads1115_t *dev) {
    for (int ch = 0; ch < 4; ch++) {
        if (dev->cal_samples_left[ch] > 0) {
            return false;
        }
    }
    return true;
}

int32_t ads1115_get_calibration(ads1115_t *dev, int channel) {
    return dev->cal_accumulator[channel];
}


int16_t ads1115_get_sample_range(int channel) {
    return (int16_t)(samples[channel].max_value - samples[channel].min_value);
}

millis_t ads1115_get_sample_millis(int channel) {
    return samples[channel].timestamp;
    //return ads1115_sample_millis[channel];
}
