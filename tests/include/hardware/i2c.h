#pragma once

#include <stdint.h>
#include <stdbool.h>

// Mock i2c types for testing
typedef struct i2c_inst i2c_inst_t;

// Mock i2c functions
static inline int i2c_write_blocking(i2c_inst_t *i2c, uint8_t addr, const uint8_t *src, size_t len, bool nostop) {
    (void)i2c; (void)addr; (void)src; (void)len; (void)nostop;
    return len;
}

static inline int i2c_read_blocking(i2c_inst_t *i2c, uint8_t addr, uint8_t *dst, size_t len, bool nostop) {
    (void)i2c; (void)addr; (void)dst; (void)len; (void)nostop;
    return len;
}
