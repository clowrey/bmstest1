#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#define ISOSPI_MASTER_SM 0

void isospi_master_setup(unsigned int tx_pin_base, unsigned int rx_pin_base);
void isospi_send_wakeup_cs_blocking();
bool isospi_write_read_blocking(uint8_t* out_buf, uint8_t* in_buf, size_t len, size_t skip);

void isospi_master_async_setup(unsigned int tx_pin_base, unsigned int rx_pin_base);
void isospi_write_read_async(uint8_t* tx_buf, uint8_t* rx_buf, size_t len, size_t skip);
bool isospi_is_busy();
bool isospi_is_valid();
