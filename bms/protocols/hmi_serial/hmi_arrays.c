#include "hmi_arrays.h"
#include "hmi_serial.h"
#include "app/model.h"

#include <string.h>

typedef struct {
    const void *values;
    uint16_t total;
    uint8_t encoding;
    millis_t sample_millis; // when the values were read, 0 = no reading yet
    bool timestamped;       // whether sample_millis is tracked for this array
} hmi_array_t;

static bool hmi_get_array(const bms_model_t *model, uint8_t array_id, hmi_array_t *array) {
    switch (array_id) {
        case HMI_ARRAY_CELL_VOLTAGES:
            *array = (hmi_array_t){
                .values = model->cell_voltages_mV,
                .total = sizeof(model->cell_voltages_mV) / sizeof(model->cell_voltages_mV[0]),
                .encoding = HMI_ARRAY_ENCODING_INT16_DELTA,
                .sample_millis = model->cell_voltages_millis,
                .timestamped = true,
            };
            return true;
        case HMI_ARRAY_BALANCING_TIMES:
            *array = (hmi_array_t){
                .values = model->balancing_sm.balance_time_remaining,
                .total = sizeof(model->balancing_sm.balance_time_remaining) / sizeof(model->balancing_sm.balance_time_remaining[0]),
                .encoding = HMI_ARRAY_ENCODING_INT16_DELTA,
            };
            return true;
        case HMI_ARRAY_MODULE_TEMPS:
            *array = (hmi_array_t){
                .values = model->module_temperatures,
                .total = sizeof(model->module_temperatures) / sizeof(model->module_temperatures[0]),
                .encoding = HMI_TYPE_FLOAT,
                .sample_millis = model->module_temperatures_millis,
                .timestamped = true,
            };
            return true;
        case HMI_ARRAY_RAW_TEMPS:
            *array = (hmi_array_t){
                .values = model->raw_temperatures,
                .total = sizeof(model->raw_temperatures) / sizeof(model->raw_temperatures[0]),
                .encoding = HMI_TYPE_UINT16,
            };
            return true;
    }
    return false;
}

static inline void put_uint16(uint8_t *buf, uint16_t value) {
    buf[0] = value & 0xFF;
    buf[1] = (value >> 8) & 0xFF;
}

static inline void put_uint32(uint8_t *buf, uint32_t value) {
    buf[0] = value & 0xFF;
    buf[1] = (value >> 8) & 0xFF;
    buf[2] = (value >> 16) & 0xFF;
    buf[3] = (value >> 24) & 0xFF;
}

size_t hmi_encode_int16_delta_array(uint8_t *buf, size_t capacity, const int16_t *values,
                                    size_t count, size_t *encoded) {
    // Absolute values are sent as two bytes, big-endian, with the MSB set (so
    // only the low 15 bits of the value are kept). Deltas from the previous
    // value in -64 to +63 are sent as one byte with the MSB clear. The
    // previous value starts at zero.

    int16_t last_value = 0;
    size_t idx = 0;
    size_t i;
    for (i = 0; i < count; i++) {
        int16_t value = values[i];
        int32_t delta = (int32_t)value - last_value;
        if (delta >= -64 && delta <= 63) {
            if (idx + 1 > capacity) break;
            buf[idx++] = delta & 0x7F;
        } else {
            if (idx + 2 > capacity) break;
            buf[idx++] = ((value >> 8) & 0xFF) | 0x80;
            buf[idx++] = value & 0xFF;
        }
        last_value = value;
    }
    if (encoded) *encoded = i;
    return idx;
}

static size_t hmi_encode_fixed_array(uint8_t *buf, size_t capacity, const hmi_array_t *array,
                                     uint16_t start, size_t count, size_t *encoded) {
    size_t size = array->encoding & 0x0F;
    size_t n = capacity / size;
    if (n > count) n = count;

    for (size_t i = 0; i < n; i++) {
        uint8_t *out = &buf[i * size];
        size_t index = start + i;
        switch (array->encoding) {
            case HMI_TYPE_FLOAT: {
                uint32_t u;
                memcpy(&u, &((const float *)array->values)[index], sizeof(u));
                put_uint32(out, u);
                break;
            }
            case HMI_TYPE_UINT16:
                put_uint16(out, ((const uint16_t *)array->values)[index]);
                break;
        }
    }
    *encoded = n;
    return n * size;
}

size_t hmi_build_read_array_response(uint8_t *buf, size_t capacity, uint8_t device_address,
                                     const bms_model_t *model, uint8_t array_id, uint16_t start) {
    hmi_array_t array;
    if (!hmi_get_array(model, array_id, &array)) {
        array = (hmi_array_t){ .total = 0, .encoding = HMI_ARRAY_ENCODING_NONE };
    }

    size_t idx = 0;
    buf[idx++] = HMI_MSG_READ_ARRAY_RESPONSE;
    buf[idx++] = device_address;
    buf[idx++] = array_id;
    buf[idx++] = array.encoding;
    put_uint16(&buf[idx], start);
    idx += 2;
    put_uint16(&buf[idx], array.total);
    idx += 2;
    size_t count_idx = idx++;
    put_uint32(&buf[idx], array.sample_millis);
    idx += 4;

    // Nothing to send past the end, or before the first reading
    size_t count = 0;
    if (start < array.total && !(array.timestamped && array.sample_millis == 0)) {
        size_t remaining = array.total - start;
        // The count is one byte
        if (remaining > 255) remaining = 255;

        if (array.encoding == HMI_ARRAY_ENCODING_INT16_DELTA) {
            idx += hmi_encode_int16_delta_array(&buf[idx], capacity - idx,
                                                &((const int16_t *)array.values)[start], remaining, &count);
        } else {
            idx += hmi_encode_fixed_array(&buf[idx], capacity - idx, &array, start, remaining, &count);
        }
    }
    buf[count_idx] = (uint8_t)count;

    return idx;
}
