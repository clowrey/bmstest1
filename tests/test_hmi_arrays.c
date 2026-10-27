#include <stddef.h>
#include <setjmp.h>
#include <stdarg.h>
#include <cmocka.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

#include "app/model.h"
#include "protocols/hmi_serial/hmi_serial.h"
#include "protocols/hmi_serial/hmi_arrays.h"

// Mock globals
millis_t stored_millis = 0;
millis64_t stored_millis64 = 0;

#define DEVICE_ADDRESS 3
#define MAX_PAYLOAD 256

bms_model_t model;

static int setup(void **state) {
    (void) state;
    memset(&model, 0, sizeof(model));
    return 0;
}

// ============================================================================
// Helpers: decode the way the HMI would
// ============================================================================

static uint16_t get_uint16(const uint8_t *buf) {
    return (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
}

static uint32_t get_uint32(const uint8_t *buf) {
    return (uint32_t)buf[0] | ((uint32_t)buf[1] << 8) | ((uint32_t)buf[2] << 16) | ((uint32_t)buf[3] << 24);
}

// Read count little-endian int16 elements
static void decode_int16(const uint8_t *buf, size_t count, int16_t *out) {
    for (size_t i = 0; i < count; i++) {
        out[i] = (int16_t)get_uint16(&buf[i * 2]);
    }
}

// Decode count values in the older delta coding, returning the bytes consumed
static size_t decode_legacy_delta(const uint8_t *buf, size_t count, int16_t *out) {
    int16_t last = 0;
    size_t idx = 0;
    for (size_t i = 0; i < count; i++) {
        if (buf[idx] & 0x80) {
            // Absolute, 15 bits, sign extended from bit 14
            uint16_t v = (uint16_t)((buf[idx] & 0x7F) << 8) | buf[idx + 1];
            if (v & 0x4000) v |= 0x8000;
            last = (int16_t)v;
            idx += 2;
        } else {
            int d = buf[idx] & 0x7F;
            if (d & 0x40) d -= 128;
            last = (int16_t)(last + d);
            idx += 1;
        }
        out[i] = last;
    }
    return idx;
}

typedef struct {
    uint8_t type;
    uint8_t address;
    uint8_t array_id;
    uint8_t encoding;
    uint16_t start;
    uint16_t total;
    uint8_t count;
    uint32_t sample_millis;
    const uint8_t *elements;
} response_t;

static response_t parse_response(const uint8_t *buf, size_t len) {
    assert_true(len >= HMI_ARRAY_RESPONSE_HEADER_LEN);
    return (response_t){
        .type = buf[0],
        .address = buf[1],
        .array_id = buf[2],
        .encoding = buf[3],
        .start = get_uint16(&buf[4]),
        .total = get_uint16(&buf[6]),
        .count = buf[8],
        .sample_millis = get_uint32(&buf[9]),
        .elements = &buf[HMI_ARRAY_RESPONSE_HEADER_LEN],
    };
}

// Read a whole int16 array page by page, as the HMI would, with packets of at
// most capacity bytes. Returns the pages used.
static int read_int16_array(uint8_t array_id, int16_t *out, uint16_t expected_total, size_t capacity) {
    uint16_t start = 0;
    int pages = 0;
    while (start < expected_total) {
        uint8_t buf[MAX_PAYLOAD];
        size_t len = hmi_build_read_array_response(buf, capacity, DEVICE_ADDRESS, &model, array_id, start);
        assert_true(len <= capacity);

        response_t r = parse_response(buf, len);
        assert_int_equal(r.start, start);
        assert_int_equal(r.total, expected_total);
        assert_int_equal(r.encoding, HMI_TYPE_INT16);
        assert_true(r.count > 0);

        decode_int16(r.elements, r.count, &out[start]);
        assert_int_equal(HMI_ARRAY_RESPONSE_HEADER_LEN + 2 * r.count, len);

        start += r.count;
        pages++;
    }
    assert_int_equal(start, expected_total);
    return pages;
}

// The encoder as it was in hmi_serial.c, for checking the legacy messages
// are unchanged
static size_t legacy_encode(uint8_t *buf, const int16_t *values, size_t count) {
    int16_t last_value = 0;
    size_t idx = 0;
    for (size_t i = 0; i < count; i++) {
        int16_t value = values[i];
        int16_t delta = value - last_value;
        if (delta >= -64 && delta <= 63) {
            buf[idx++] = (delta & 0x7F);
        } else {
            buf[idx++] = ((value >> 8) & 0xFF) | 0x80;
            buf[idx++] = (value >> 0) & 0xFF;
        }
        last_value = value;
    }
    return idx;
}

// ============================================================================
// Older delta coding (READ_CELL_VOLTAGES / READ_BALANCING_TIMES)
// ============================================================================

static void test_delta_round_trip(void **state) {
    (void) state;
    const int16_t values[] = { 3300, 3301, 3299, 3363, 3236, 3300, -1, -1, 0, 1, 16383, -16384, 63, -64, 64 };
    const size_t n = sizeof(values) / sizeof(values[0]);

    uint8_t buf[64];
    size_t encoded;
    size_t len = hmi_encode_int16_delta_array(buf, sizeof(buf), values, n, &encoded);
    assert_int_equal(encoded, n);

    int16_t out[sizeof(values) / sizeof(values[0])];
    assert_int_equal(decode_legacy_delta(buf, n, out), len);
    assert_memory_equal(out, values, sizeof(values));
}

static void test_delta_matches_legacy_encoder(void **state) {
    (void) state;
    int16_t values[120];
    for (int i = 0; i < 120; i++) {
        values[i] = (int16_t)(3300 + (i * 37) % 200 - 100);
    }
    values[17] = -1; // an absent channel

    uint8_t expected[243], actual[243];
    size_t expected_len = legacy_encode(expected, values, 120);
    size_t actual_len = hmi_encode_int16_delta_array(actual, sizeof(actual), values, 120, NULL);

    assert_int_equal(actual_len, expected_len);
    assert_memory_equal(actual, expected, expected_len);
}

static void test_delta_stops_at_capacity(void **state) {
    (void) state;
    // Each value is an absolute (two bytes); 5 bytes of room fits two of them
    const int16_t values[] = { 1000, 2000, 3000, 4000 };
    uint8_t buf[8];
    memset(buf, 0xAA, sizeof(buf));

    size_t encoded;
    size_t len = hmi_encode_int16_delta_array(buf, 5, values, 4, &encoded);

    assert_int_equal(encoded, 2);
    assert_int_equal(len, 4);
    // Nothing written past the values that fit
    assert_int_equal(buf[4], 0xAA);
}

static void test_delta_large_jump_not_wrapped(void **state) {
    (void) state;
    // The difference overflows int16, so must not be sent as a small delta
    const int16_t values[] = { -16384, 16383 };
    uint8_t buf[8];
    size_t len = hmi_encode_int16_delta_array(buf, sizeof(buf), values, 2, NULL);
    assert_int_equal(len, 4);

    int16_t out[2];
    decode_legacy_delta(buf, 2, out);
    assert_memory_equal(out, values, sizeof(values));
}

// ============================================================================
// Responses
// ============================================================================

static void test_cell_voltages_header(void **state) {
    (void) state;
    for (int i = 0; i < 120; i++) model.cell_voltages_mV[i] = 3300;
    model.cell_voltages_millis = 123456;

    uint8_t buf[MAX_PAYLOAD];
    size_t len = hmi_build_read_array_response(buf, sizeof(buf), DEVICE_ADDRESS, &model, HMI_ARRAY_CELL_VOLTAGES, 0);
    response_t r = parse_response(buf, len);

    assert_int_equal(r.type, HMI_MSG_READ_ARRAY_RESPONSE);
    assert_int_equal(r.address, DEVICE_ADDRESS);
    assert_int_equal(r.array_id, HMI_ARRAY_CELL_VOLTAGES);
    assert_int_equal(r.encoding, HMI_TYPE_INT16);
    assert_int_equal(r.start, 0);
    assert_int_equal(r.total, 120);
    assert_int_equal(r.count, 120);
    assert_int_equal(r.sample_millis, 123456);
    // 120 cells fit one packet: 13 + 240 = 253 bytes
    assert_int_equal(len, HMI_ARRAY_RESPONSE_HEADER_LEN + 2 * 120);
    assert_true(len <= MAX_PAYLOAD);

    int16_t out[120];
    decode_int16(r.elements, 120, out);
    assert_memory_equal(out, model.cell_voltages_mV, sizeof(out));
}

static void test_cell_voltages_full_range(void **state) {
    (void) state;
    // Every int16 value survives, including -1 for an absent channel
    const int16_t values[] = { -1, INT16_MIN, INT16_MAX, 0, 1, 16384, -16385, 3300 };
    for (int i = 0; i < 120; i++) model.cell_voltages_mV[i] = values[i % 8];
    model.cell_voltages_millis = 1;

    int16_t out[120];
    int pages = read_int16_array(HMI_ARRAY_CELL_VOLTAGES, out, 120, MAX_PAYLOAD);

    assert_int_equal(pages, 1);
    assert_memory_equal(out, model.cell_voltages_mV, sizeof(out));
}

static void test_cell_voltages_paged(void **state) {
    (void) state;
    // A smaller packet stands in for an array too big for one packet
    for (int i = 0; i < 120; i++) model.cell_voltages_mV[i] = (int16_t)(3000 + i);
    model.cell_voltages_mV[50] = -1;
    model.cell_voltages_millis = 1;

    int16_t out[120];
    int pages = read_int16_array(HMI_ARRAY_CELL_VOLTAGES, out, 120, HMI_ARRAY_RESPONSE_HEADER_LEN + 101);

    // 50 values (100 bytes) per page
    assert_int_equal(pages, 3);
    assert_memory_equal(out, model.cell_voltages_mV, sizeof(out));
}

static void test_cell_voltages_from_middle(void **state) {
    (void) state;
    for (int i = 0; i < 120; i++) model.cell_voltages_mV[i] = (int16_t)(3000 + i * 5);
    model.cell_voltages_millis = 1;

    uint8_t buf[MAX_PAYLOAD];
    size_t len = hmi_build_read_array_response(buf, sizeof(buf), DEVICE_ADDRESS, &model, HMI_ARRAY_CELL_VOLTAGES, 100);
    response_t r = parse_response(buf, len);

    assert_int_equal(r.start, 100);
    assert_int_equal(r.count, 20);
    int16_t out[20];
    decode_int16(r.elements, 20, out);
    assert_memory_equal(out, &model.cell_voltages_mV[100], sizeof(out));
}

static void test_cell_voltages_not_read_yet(void **state) {
    (void) state;
    model.cell_voltages_millis = 0;

    uint8_t buf[MAX_PAYLOAD];
    size_t len = hmi_build_read_array_response(buf, sizeof(buf), DEVICE_ADDRESS, &model, HMI_ARRAY_CELL_VOLTAGES, 0);
    response_t r = parse_response(buf, len);

    assert_int_equal(r.total, 120);
    assert_int_equal(r.count, 0);
    assert_int_equal(r.sample_millis, 0);
    assert_int_equal(len, HMI_ARRAY_RESPONSE_HEADER_LEN);
}

static void test_start_past_end(void **state) {
    (void) state;
    model.cell_voltages_millis = 1;

    uint8_t buf[MAX_PAYLOAD];
    size_t len = hmi_build_read_array_response(buf, sizeof(buf), DEVICE_ADDRESS, &model, HMI_ARRAY_CELL_VOLTAGES, 120);
    response_t r = parse_response(buf, len);

    assert_int_equal(r.start, 120);
    assert_int_equal(r.total, 120);
    assert_int_equal(r.count, 0);
    assert_int_equal(len, HMI_ARRAY_RESPONSE_HEADER_LEN);
}

static void test_unknown_array(void **state) {
    (void) state;
    uint8_t buf[MAX_PAYLOAD];
    size_t len = hmi_build_read_array_response(buf, sizeof(buf), DEVICE_ADDRESS, &model, 200, 0);
    response_t r = parse_response(buf, len);

    assert_int_equal(r.type, HMI_MSG_READ_ARRAY_RESPONSE);
    assert_int_equal(r.array_id, 200);
    assert_int_equal(r.encoding, HMI_ARRAY_ENCODING_NONE);
    assert_int_equal(r.total, 0);
    assert_int_equal(r.count, 0);
    assert_int_equal(len, HMI_ARRAY_RESPONSE_HEADER_LEN);
}

static void test_balancing_times_untimestamped(void **state) {
    (void) state;
    // Balancing times have no sample time, so are sent straight away. They
    // run up to INT16_MAX, beyond what the older delta coding can carry.
    for (int i = 0; i < 120; i++) model.balancing_sm.balance_time_remaining[i] = (int16_t)(i * 3);
    model.balancing_sm.balance_time_remaining[10] = INT16_MAX;
    model.balancing_sm.balance_time_remaining[11] = 0;
    model.balancing_sm.balance_time_remaining[12] = 16384;
    model.balancing_sm.balance_time_remaining[13] = 20000;

    int16_t out[120];
    read_int16_array(HMI_ARRAY_BALANCING_TIMES, out, 120, MAX_PAYLOAD);
    assert_memory_equal(out, model.balancing_sm.balance_time_remaining, sizeof(out));
}

static void test_module_temps_floats(void **state) {
    (void) state;
    for (int i = 0; i < NUM_MODULE_TEMPS; i++) model.module_temperatures[i] = 20.5f + i;
    model.module_temperatures[0] = NAN;
    model.module_temperatures_millis = 42;

    uint8_t buf[MAX_PAYLOAD];
    size_t len = hmi_build_read_array_response(buf, sizeof(buf), DEVICE_ADDRESS, &model, HMI_ARRAY_MODULE_TEMPS, 0);
    response_t r = parse_response(buf, len);

    assert_int_equal(r.encoding, HMI_TYPE_FLOAT);
    assert_int_equal(r.total, NUM_MODULE_TEMPS);
    assert_int_equal(r.count, NUM_MODULE_TEMPS);
    assert_int_equal(r.sample_millis, 42);
    assert_int_equal(len, HMI_ARRAY_RESPONSE_HEADER_LEN + 4 * NUM_MODULE_TEMPS);

    for (int i = 0; i < NUM_MODULE_TEMPS; i++) {
        uint32_t u = get_uint32(&r.elements[i * 4]);
        float f;
        memcpy(&f, &u, sizeof(f));
        if (i == 0) {
            assert_true(isnan(f));
        } else {
            assert_true(f == model.module_temperatures[i]);
        }
    }
}

static void test_raw_temps_uint16_paged_by_capacity(void **state) {
    (void) state;
    const int total = sizeof(model.raw_temperatures) / sizeof(model.raw_temperatures[0]);
    for (int i = 0; i < total; i++) model.raw_temperatures[i] = (uint16_t)(0xF000 + i);

    // A small buffer forces several pages: room for 10 elements each
    const size_t capacity = HMI_ARRAY_RESPONSE_HEADER_LEN + 2 * 10 + 1;
    uint16_t start = 0;
    while (start < total) {
        uint8_t buf[MAX_PAYLOAD];
        size_t len = hmi_build_read_array_response(buf, capacity, DEVICE_ADDRESS, &model, HMI_ARRAY_RAW_TEMPS, start);
        assert_true(len <= capacity);
        response_t r = parse_response(buf, len);

        assert_int_equal(r.encoding, HMI_TYPE_UINT16);
        assert_int_equal(r.total, total);
        assert_int_equal(r.count, (total - start) < 10 ? (total - start) : 10);
        for (int i = 0; i < r.count; i++) {
            assert_int_equal(get_uint16(&r.elements[i * 2]), model.raw_temperatures[start + i]);
        }
        start += r.count;
    }
}

int main(void) {
    const struct CMUnitTest tests[] = {
        // Older delta coding
        cmocka_unit_test_setup(test_delta_round_trip, setup),
        cmocka_unit_test_setup(test_delta_matches_legacy_encoder, setup),
        cmocka_unit_test_setup(test_delta_stops_at_capacity, setup),
        cmocka_unit_test_setup(test_delta_large_jump_not_wrapped, setup),

        // Responses
        cmocka_unit_test_setup(test_cell_voltages_header, setup),
        cmocka_unit_test_setup(test_cell_voltages_full_range, setup),
        cmocka_unit_test_setup(test_cell_voltages_paged, setup),
        cmocka_unit_test_setup(test_cell_voltages_from_middle, setup),
        cmocka_unit_test_setup(test_cell_voltages_not_read_yet, setup),
        cmocka_unit_test_setup(test_start_past_end, setup),
        cmocka_unit_test_setup(test_unknown_array, setup),
        cmocka_unit_test_setup(test_balancing_times_untimestamped, setup),
        cmocka_unit_test_setup(test_module_temps_floats, setup),
        cmocka_unit_test_setup(test_raw_temps_uint16_paged_by_capacity, setup),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
