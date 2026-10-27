#pragma once

// Paged reads of the model's arrays for the HMI (HMI_MSG_READ_ARRAY). Kept
// apart from hmi_serial.c, and free of the hardware, so the host tests can
// build the responses.

#include <stddef.h>
#include <stdint.h>

typedef struct bms_model bms_model_t;

// Array IDs
#define HMI_ARRAY_CELL_VOLTAGES   1 // int16 (mV)
#define HMI_ARRAY_BALANCING_TIMES 2 // int16 (balancing periods remaining)
#define HMI_ARRAY_MODULE_TEMPS    3 // float (C)
#define HMI_ARRAY_RAW_TEMPS       4 // uint16, raw BMB temperature readings

// The encoding byte of a response is the elements' HMI_TYPE_*, each element in
// that type's usual little-endian form. The top bit (0x80) is reserved for a
// packed form of the type, should one be needed (see hmi_serial.h, 2.9).

// Encoding of an unknown array, which has no elements
#define HMI_ARRAY_ENCODING_NONE 0x00

// Bytes before the elements in a READ_ARRAY_RESPONSE
#define HMI_ARRAY_RESPONSE_HEADER_LEN 13

// Delta code up to count values into buf in the older coding of
// READ_CELL_VOLTAGES_RESPONSE and READ_BALANCING_TIMES_RESPONSE, which keeps
// only 15 bits of each value. Stops before the first value that wouldn't fit
// in capacity bytes. Returns the bytes written; *encoded (if not NULL) is set
// to the values written.
size_t hmi_encode_int16_delta_array(uint8_t *buf, size_t capacity, const int16_t *values,
                                    size_t count, size_t *encoded);

// Build a READ_ARRAY_RESPONSE in buf holding as many elements from start
// onwards as fit in capacity bytes. Returns the response length.
size_t hmi_build_read_array_response(uint8_t *buf, size_t capacity, uint8_t device_address,
                                     const bms_model_t *model, uint8_t array_id, uint16_t start);
