#pragma once

#include "hardware/i2c.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef void (*i2c_async_callback_t)(i2c_inst_t *i2c, bool success, void *user_data);

/**
 * @brief Initialize I2C instance with pins and baudrate.
 * 
 * @param i2c I2C instance (i2c0 or i2c1)
 * @param baudrate Clock speed in Hz
 * @param sda_pin GPIO for SDA
 * @param scl_pin GPIO for SCL
 */
void i2c_async_init(i2c_inst_t *i2c, uint baudrate, uint sda_pin, uint scl_pin);

/**
 * @brief Start a non-blocking register read.
 * 
 * This will write the register address, then perform a restart and read the requested length.
 * 
 * @param i2c I2C instance
 * @param addr 7-bit peripheral address
 * @param reg Register address to read from
 * @param buffer Buffer to store read data
 * @param len Number of bytes to read
 * @param callback Callback to fire on completion
 * @param user_data User data to pass to the callback
 * @return true if the transaction was started
 * @return false if the driver is busy
 */
bool i2c_async_read_reg(i2c_inst_t *i2c, uint8_t addr, uint8_t reg, uint8_t *buffer, size_t len, i2c_async_callback_t callback, void *user_data);

/**
 * @brief Start a non-blocking dual register read.
 * 
 * This will write the first register address, perform a restart and read len1,
 * then write the second register address, perform a restart and read len2.
 * 
 * @param i2c I2C instance
 * @param addr 7-bit peripheral address
 * @param reg1 First register address
 * @param len1 Number of bytes to read from reg1
 * @param reg2 Second register address
 * @param len2 Number of bytes to read from reg2
 * @param buffer Buffer to store all read data (must be at least len1 + len2 bytes)
 * @param callback Callback to fire on completion
 * @param user_data User data to pass to the callback
 * @return true if the transaction was started
 */
bool i2c_async_read_regs_dual(i2c_inst_t *i2c, uint8_t addr, uint8_t reg1, size_t len1, uint8_t reg2, size_t len2, uint8_t *buffer, i2c_async_callback_t callback, void *user_data);

/**
 * @brief Start a non-blocking register write.
 * 
 * This will write the register address followed by the data bytes.
 * 
 * @param i2c I2C instance
 * @param addr 7-bit peripheral address
 * @param reg Register address to write to
 * @param data Data to write
 * @param len Number of bytes to write
 * @param callback Callback to fire on completion
 * @param user_data User data to pass to the callback
 * @return true if the transaction was started
 * @return false if the driver is busy
 */
bool i2c_async_write_reg(i2c_inst_t *i2c, uint8_t addr, uint8_t reg, const uint8_t *data, size_t len, i2c_async_callback_t callback, void *user_data);

/**
 * @brief Synchronous (blocking) register read.
 * 
 * This will wait until the transaction is complete.
 * 
 * @param i2c I2C instance
 * @param addr 7-bit peripheral address
 * @param reg Register address to read from
 * @param buffer Buffer to store read data
 * @param len Number of bytes to read
 * @param timeout_us Timeout in microseconds
 * @return true if successful
 */
bool i2c_blocking_read_reg(i2c_inst_t *i2c, uint8_t addr, uint8_t reg, uint8_t *buffer, size_t len, uint32_t timeout_us);

/**
 * @brief Synchronous (blocking) register write.
 * 
 * This will wait until the transaction is complete.
 * 
 * @param i2c I2C instance
 * @param addr 7-bit peripheral address
 * @param reg Register address to write to
 * @param data Data to write
 * @param len Number of bytes to write
 * @param timeout_us Timeout in microseconds
 * @return true if successful
 */
bool i2c_blocking_write_reg(i2c_inst_t *i2c, uint8_t addr, uint8_t reg, const uint8_t *data, size_t len, uint32_t timeout_us);

/**
 * @brief Check if a transaction is currently in progress.
 * 
 * @param i2c I2C instance
 * @return true if busy
 */
bool i2c_async_is_busy(i2c_inst_t *i2c);

/**
 * @brief Check if the last transaction completed successfully.
 * 
 * @param i2c I2C instance
 * @return true if successful
 */
bool i2c_async_last_success(i2c_inst_t *i2c);
