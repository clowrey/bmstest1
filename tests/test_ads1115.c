#include <stddef.h>
#include <setjmp.h>
#include <stdarg.h>
#include <cmocka.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "config/allocations.h"
#include "drivers/sensors/ads1115.h"
#include "drivers/chip/i2c.h"
#include "sys/time/time.h"

// Exercises the ADS1115 scan against fake I2C and timer drivers. Transactions
// and alarms are queued rather than completed immediately, so the test steps
// the scan one event at a time and can make any individual request fail.

// Mock globals
millis64_t stored_millis64 = 0;
millis_t stored_millis = 0;

uint32_t time_us_32() { return stored_millis * 1000; }

#include "sys/logging/logging.h"
void logging_printf(log_level_t level, const char *format, ...) {
    (void)level;
    (void)format;
}

// ============================================================================
// Fake I2C driver
// ============================================================================

static struct {
    bool pending;
    i2c_async_callback_t callback;
    void *user_data;
    uint8_t addr;
    uint8_t *read_buffer;
    size_t read_len;
    int refuse_countdown; // refuse the request when this reaches 0, -1 = never
    int overlaps;         // requests made while one was already in flight
} fake_i2c = { .refuse_countdown = -1 };

// Last config written to each chip (0x48, 0x49)
static uint16_t fake_config[2];

// The conversion result the fake returns, distinct for each chip and input
// mux, so tests can check each reading lands on the right logical channel
static int16_t fake_conversion(uint8_t addr, uint16_t config) {
    return (int16_t)(100 * (addr - 0x47) + ((config >> 12) & 0x7) + 1);
}

static bool fake_i2c_start(uint8_t addr, uint8_t *read_buffer, size_t read_len, i2c_async_callback_t callback, void *user_data) {
    if (fake_i2c.pending) {
        fake_i2c.overlaps++;
        return false;
    }
    if (fake_i2c.refuse_countdown >= 0 && fake_i2c.refuse_countdown-- == 0) {
        // Refused, as the real driver does when busy or the TX FIFO is full
        return false;
    }
    fake_i2c.pending = true;
    fake_i2c.addr = addr;
    fake_i2c.callback = callback;
    fake_i2c.user_data = user_data;
    fake_i2c.read_buffer = read_buffer;
    fake_i2c.read_len = read_len;
    return true;
}

static void fake_i2c_complete(bool success) {
    fake_i2c.pending = false;
    if (success && fake_i2c.read_buffer) {
        assert_int_equal(fake_i2c.read_len, 2);
        int16_t value = fake_conversion(fake_i2c.addr, fake_config[fake_i2c.addr - 0x48]);
        fake_i2c.read_buffer[0] = (uint8_t)(value >> 8);
        fake_i2c.read_buffer[1] = (uint8_t)value;
    }
    fake_i2c.callback(ADS1115_I2C, success, fake_i2c.user_data);
}

void i2c_async_init(i2c_inst_t *i2c, uint baudrate, uint sda_pin, uint scl_pin) {
    (void)i2c; (void)baudrate; (void)sda_pin; (void)scl_pin;
}

bool i2c_blocking_read_reg(i2c_inst_t *i2c, uint8_t addr, uint8_t reg, uint8_t *buffer, size_t len, uint32_t timeout_us) {
    // Used by init to probe for each chip; all chips are present
    (void)i2c; (void)addr; (void)reg; (void)buffer; (void)len; (void)timeout_us;
    return true;
}

bool i2c_async_write_reg(i2c_inst_t *i2c, uint8_t addr, uint8_t reg, const uint8_t *data, size_t len, i2c_async_callback_t callback, void *user_data) {
    (void)i2c;
    bool started = fake_i2c_start(addr, NULL, 0, callback, user_data);
    if (started && reg == ADS1115_REG_CONFIG && len == 2) {
        fake_config[addr - 0x48] = (uint16_t)((data[0] << 8) | data[1]);
    }
    return started;
}

bool i2c_async_read_reg(i2c_inst_t *i2c, uint8_t addr, uint8_t reg, uint8_t *buffer, size_t len, i2c_async_callback_t callback, void *user_data) {
    (void)i2c; (void)reg;
    return fake_i2c_start(addr, buffer, len, callback, user_data);
}

// ============================================================================
// Fake timers
// ============================================================================

static struct {
    bool pending;
    alarm_callback_t callback;
    void *user_data;
    int fail_countdown; // fail the request when this reaches 0, -1 = never
} fake_alarm = { .fail_countdown = -1 };

static repeating_timer_callback_t watchdog_callback;

alarm_id_t add_alarm_in_ms(uint32_t ms, alarm_callback_t callback, void *user_data, bool fire_if_past) {
    (void)ms; (void)fire_if_past;
    if (fake_alarm.fail_countdown >= 0 && fake_alarm.fail_countdown-- == 0) {
        // No free alarm slots
        return -1;
    }
    assert_false(fake_alarm.pending);
    fake_alarm.pending = true;
    fake_alarm.callback = callback;
    fake_alarm.user_data = user_data;
    return 1;
}

bool add_repeating_timer_ms(int32_t delay_ms, repeating_timer_callback_t callback, void *user_data, struct repeating_timer *out) {
    (void)delay_ms; (void)user_data; (void)out;
    watchdog_callback = callback;
    return true;
}

static void fire_alarm(void) {
    fake_alarm.pending = false;
    fake_alarm.callback(1, fake_alarm.user_data);
}

static void fire_watchdog(void) {
    watchdog_callback(NULL);
}

// ============================================================================
// Helpers
// ============================================================================

// Complete pending I2C transactions and alarms until nothing is left in
// flight (the scan has stalled or been abandoned) or max_events is reached.
// Returns the number of events processed.
static int run_events(int max_events) {
    int events = 0;
    while (events < max_events) {
        if (fake_i2c.pending) {
            fake_i2c_complete(true);
        } else if (fake_alarm.pending) {
            fire_alarm();
        } else {
            break;
        }
        events++;
    }
    return events;
}

// Comfortably more events than a full scan cycle on either board rev
#define EVENTS_PER_FEW_CYCLES 200

static bool all_channels_sampled_since(millis_t t) {
    for (int ch = 0; ch < ADS1115_CHANNEL_COUNT; ch++) {
        if (ads1115_get_sample_millis(ch) < t) {
            return false;
        }
    }
    return true;
}

// Advance time and check the scan delivers a fresh sample on every channel
static void assert_scan_recovers(void) {
    stored_millis += 1000;
    millis_t t = stored_millis;

    // Like the real watchdog, this must be harmless if the scan is running
    fire_watchdog();
    run_events(EVENTS_PER_FEW_CYCLES);

    assert_true(all_channels_sampled_since(t));
    assert_int_equal(fake_i2c.overlaps, 0);
}

// Make the n'th I2C request from now get refused, run until the scan stops,
// then check the watchdog gets it going again.
static void refuse_nth_i2c_request_and_recover(int n) {
    fake_i2c.refuse_countdown = n;
    run_events(EVENTS_PER_FEW_CYCLES);
    // The refusal must have stopped the scan with nothing left in flight
    assert_int_equal(fake_i2c.refuse_countdown, -1);
    assert_false(fake_i2c.pending);
    assert_false(fake_alarm.pending);

    assert_scan_recovers();
}

static void fail_nth_alarm_and_recover(int n) {
    fake_alarm.fail_countdown = n;
    run_events(EVENTS_PER_FEW_CYCLES);
    assert_int_equal(fake_alarm.fail_countdown, -1);
    assert_false(fake_i2c.pending);
    assert_false(fake_alarm.pending);

    assert_scan_recovers();
}

// ============================================================================
// Tests
// ============================================================================

static int setup(void **state) {
    (void)state;
    static bool initialized = false;
    if (!initialized) {
        stored_millis = 1000;
        assert_true(ads1115_init(&ads1115_dev, 0x48));
#if HAS_ADS1115_SECONDARY()
        assert_true(ads1115_init(&ads1115_dev_b, 0x49));
#endif
        // Watchdog's first fire starts the free-running scan
        fire_watchdog();
        initialized = true;
    }
    return 0;
}

static void test_scan_samples_all_channels(void **state) {
    (void)state;
    assert_scan_recovers();
}

static void test_scan_is_free_running(void **state) {
    (void)state;
    // Without any further watchdog fires, the scan keeps going on its own
    stored_millis += 1000;
    millis_t t = stored_millis;
    assert_int_equal(run_events(EVENTS_PER_FEW_CYCLES), EVENTS_PER_FEW_CYCLES);
    assert_true(all_channels_sampled_since(t));
}

static void test_recovers_from_i2c_error(void **state) {
    (void)state;
    run_events(5);
    assert_true(fake_i2c.pending || fake_alarm.pending);
    // Fail whichever I2C transaction comes next
    while (!fake_i2c.pending) {
        fire_alarm();
    }
    fake_i2c_complete(false);
    assert_false(fake_i2c.pending);
    assert_false(fake_alarm.pending);

    assert_scan_recovers();
}

static void test_recovers_from_refused_i2c_request(void **state) {
    (void)state;
    // Refuse each request position across more than a whole cycle, covering
    // config writes and conversion reads for every device and round
    for (int n = 0; n < 40; n++) {
        refuse_nth_i2c_request_and_recover(n);
    }
}

static void test_recovers_from_alarm_failure(void **state) {
    (void)state;
    for (int n = 0; n < 8; n++) {
        fail_nth_alarm_and_recover(n);
    }
}

// The value each logical channel should read: its chip and input mux, as
// documented in ads1115.h
static int16_t expected_conversion(int channel) {
    static const struct { uint8_t addr; uint16_t mux; } chans[] = {
        { 0x48, ADS1115_CONFIG_MUX_DIFF_0_1 },
        { 0x48, ADS1115_CONFIG_MUX_DIFF_2_3 },
        { 0x48, ADS1115_CONFIG_MUX_DIFF_1_3 },
        { 0x48, ADS1115_CONFIG_MUX_DIFF_0_3 },
        { 0x48, ADS1115_CONFIG_MUX_SINGLE_0 },
        { 0x48, ADS1115_CONFIG_MUX_SINGLE_1 },
#if HAS_ADS1115_SECONDARY()
        { 0x49, ADS1115_CONFIG_MUX_DIFF_2_3 },
        { 0x49, ADS1115_CONFIG_MUX_DIFF_0_3 },
        { 0x49, ADS1115_CONFIG_MUX_DIFF_1_3 },
        { 0x49, ADS1115_CONFIG_MUX_SINGLE_1 },
#endif
    };
    _Static_assert(sizeof(chans) / sizeof(chans[0]) == ADS1115_CHANNEL_COUNT, "one entry per channel");
    return fake_conversion(chans[channel].addr, chans[channel].mux);
}

#define CAL_SAMPLES 8

static void run_calibration(void) {
    // Start part way through a cycle, as calibration can
    run_events(7);
    ads1115_start_calibration(CAL_SAMPLES);
    assert_false(ads1115_calibration_finished());

    for (int i = 0; i < 50 && !ads1115_calibration_finished(); i++) {
        run_events(EVENTS_PER_FEW_CYCLES / 4);
    }
    assert_true(ads1115_calibration_finished());
}

static void test_calibration_sums_each_channel(void **state) {
    (void)state;
    run_calibration();

    // Exactly CAL_SAMPLES readings, each from the right chip and input
    for (int ch = 0; ch < ADS1115_CHANNEL_COUNT; ch++) {
        assert_int_equal(ads1115_get_calibration(ch), CAL_SAMPLES * expected_conversion(ch));
    }
}

#if HAS_ADS1115_SECONDARY()
static void test_calibration_finishes_without_adc_b(void **state) {
    (void)state;
    // As if ADC B wasn't detected at startup
    ads1115_dev_b.present = false;
    run_calibration();
    ads1115_dev_b.present = true;

    for (int ch = 0; ch < ADS1115_CHANNEL_COUNT; ch++) {
        // Channels 6 onwards are on ADC B
        int32_t expected = ch >= 6 ? 0 : CAL_SAMPLES * expected_conversion(ch);
        assert_int_equal(ads1115_get_calibration(ch), expected);
    }
}
#endif

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup(test_scan_samples_all_channels, setup),
        cmocka_unit_test_setup(test_scan_is_free_running, setup),
        cmocka_unit_test_setup(test_recovers_from_i2c_error, setup),
        cmocka_unit_test_setup(test_recovers_from_refused_i2c_request, setup),
        cmocka_unit_test_setup(test_recovers_from_alarm_failure, setup),
        cmocka_unit_test_setup(test_calibration_sums_each_channel, setup),
#if HAS_ADS1115_SECONDARY()
        cmocka_unit_test_setup(test_calibration_finishes_without_adc_b, setup),
#endif
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
