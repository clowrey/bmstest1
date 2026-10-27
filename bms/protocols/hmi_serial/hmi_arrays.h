#pragma once

// Paged reads of the model's arrays for the HMI (HMI_MSG_READ_ARRAY). Kept
// apart from hmi_serial.c, and free of the hardware, so the host tests can
// build the responses.

#include <stddef.h>
#include <stdint.h>

typedef struct bms_model bms_model_t;

// Array IDs
#define HMI_ARRAY_CELL_VOLTAGES   1 // int16 (mV), delta coded
#define HMI_ARRAY_BALANCING_TIMES 2 // int16 (balancing periods remaining), delta coded
#define HMI_ARRAY_MODULE_TEMPS    3 // float (C)
#define HMI_ARRAY_RAW_TEMPS       4 // uint16, raw BMB temperature readings

// Element encodings. Anything else is one of the HMI_TYPE_* values, with each
// element in that type's usual little-endian form.
#define HMI_ARRAY_ENCODING_NONE        0x00 // unknown array, no elements
#define HMI_ARRAY_ENCODING_INT16_DELTA 0x01 // see hmi_encode_int16_delta_array()

// Bytes before the elements in a READ_ARRAY_RESPONSE
#define HMI_ARRAY_RESPONSE_HEADER_LEN 13

// Delta code up to count values into buf, stopping before the first value
// that wouldn't fit in capacity bytes. Each call starts from zero, so every
// page decodes on its own. Returns the bytes written; *encoded (if not NULL)
// is set to the values written.
size_t hmi_encode_int16_delta_array(uint8_t *buf, size_t capacity, const int16_t *values,
                                    size_t count, size_t *encoded);

// Build a READ_ARRAY_RESPONSE in buf holding as many elements from start
// onwards as fit in capacity bytes. Returns the response length.
size_t hmi_build_read_array_response(uint8_t *buf, size_t capacity, uint8_t device_address,
                                     const bms_model_t *model, uint8_t array_id, uint16_t start);
