#include "isospi_master.h"

#include "config/allocations.h"

#include "hardware/irq.h"
#include "hardware/pio.h"

static struct {
    uint8_t* tx_buf;
    uint8_t* rx_buf;
    size_t len;
    size_t skip;
    size_t tx_idx;
    size_t rx_idx;
    uint32_t carry_word;
    bool valid;
    volatile bool busy;
} async_state;

static void __not_in_flash_func(isospi_irq_handler)() {
    while (!pio_sm_is_rx_fifo_empty(ISOSPI_MASTER_PIO, ISOSPI_MASTER_SM)) {
        uint32_t v = pio_sm_get(ISOSPI_MASTER_PIO, ISOSPI_MASTER_SM);
        
        // Use the MSB nibble from the carry, and store the LSB nibble as the
        // next carry.
        uint32_t carry_word = async_state.carry_word;
        int new_carry = (v & 0xf) << 28;
        v = (v >> 4) | carry_word;
        async_state.carry_word = new_carry;

        if(async_state.rx_idx >= async_state.skip) {
            uint8_t byte = 0;
            for(int r=0; r<8; r++) {
                uint8_t nibble = (v >> 28) & 0xf;
                v <<= 4;
                if(nibble==0b1001) {
                    byte = (byte << 1) | 0x1;
                } else if(nibble==0b0110) {
                    byte = (byte << 1) | 0x0;
                } else {
                    async_state.valid = false;
                    byte = (byte << 1) | 0x0;
                }
            }
            async_state.rx_buf[async_state.rx_idx - async_state.skip] = byte;
        }

        async_state.rx_idx++;

        if (async_state.tx_idx < async_state.len) {
            pio_sm_put(ISOSPI_MASTER_PIO, ISOSPI_MASTER_SM, async_state.tx_buf[async_state.tx_idx++] << 24);
        }

        if (async_state.rx_idx == async_state.len) {
            pio_set_irq0_source_enabled(ISOSPI_MASTER_PIO, pis_sm0_rx_fifo_not_empty, false);
            
            // perform final ending chip select
            // (Note: we use the blocking CS and flush here for simplicity,
            // they are relatively fast)
            busy_wait_us(1);
            isospi_master_cs(false);
            isospi_master_flush();

            async_state.busy = false;
        }
    }
}

void isospi_master_async_setup(unsigned int tx_pin_base, uint rx_pin_base) {
    // tx_pin_base      is the driver enable pin (active high)
    // tx_pin_base + 1  is the tx data pin (noninverting)

    // rx_pin_base      is the high rx data pin
    // rx_pin_base + 1  is the low rx data pin

    isospi_master_program_init(ISOSPI_MASTER_PIO, tx_pin_base, rx_pin_base);

    irq_set_exclusive_handler(ISOSPI_MASTER_PIO_IRQ0, isospi_irq_handler);
    irq_set_enabled(ISOSPI_MASTER_PIO_IRQ0, true);
}


void isospi_write_read_async(uint8_t* tx_buf, uint8_t* rx_buf, size_t len, size_t skip) {
    if (async_state.busy) return;

    async_state.tx_buf = tx_buf;
    async_state.rx_buf = rx_buf;
    async_state.len = len;
    async_state.skip = skip;
    async_state.tx_idx = 0;
    async_state.rx_idx = 0;
    async_state.carry_word = 0;
    async_state.valid = true;
    async_state.busy = true;

    isospi_master_cs(true);
    busy_wait_us(1);

    // Initial fill of the TX FIFO (up to 4 bytes)
    while (async_state.tx_idx < len && !pio_sm_is_tx_fifo_full(ISOSPI_MASTER_PIO, ISOSPI_MASTER_SM)) {
        pio_sm_put(ISOSPI_MASTER_PIO, ISOSPI_MASTER_SM, async_state.tx_buf[async_state.tx_idx++] << 24);
    }

    // Enable the RX FIFO not empty interrupt
    pio_set_irq0_source_enabled(ISOSPI_MASTER_PIO, pis_sm0_rx_fifo_not_empty, true);
}

bool isospi_is_busy() {
    return async_state.busy;
}

bool isospi_is_valid() {
    return async_state.valid;
}
