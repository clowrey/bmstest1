#include "isosnoop.h"
#include "isosnoop.pio.h"

#include "../allocations.h"

#include "hardware/dma.h"
#include "hardware/pio.h"

#include <stdio.h>

#define ISOSNOOP_SM 0

#define DMA_IRQ_PRIORITY PICO_SHARED_IRQ_HANDLER_DEFAULT_ORDER_PRIORITY
#define PIO_IRQ_PRIORITY PICO_SHARED_IRQ_HANDLER_DEFAULT_ORDER_PRIORITY
static uint dma_channel_rx;
dma_channel_hw_t *dma_chan;

#define ISOSNOOP_BUFFER_BITS 15 // 2**n byte buffer

char read_buffer[1<<ISOSNOOP_BUFFER_BITS] __attribute__((aligned(1<<ISOSNOOP_BUFFER_BITS)));
uint32_t last_write_addr;

void isosnoop_dma_setup(PIO pio, uint sm);

void isosnoop_setup(uint rx_pin_base, uint sampling_pin, uint rx_and_pin, uint timer_disable_pin) {
    isosnoop_pio_setup(ISOSNOOP_PIO, rx_pin_base, sampling_pin, rx_and_pin, timer_disable_pin);
    isosnoop_dma_setup(ISOSNOOP_PIO, ISOSNOOP_SM);

    dma_chan = dma_channel_hw_addr(dma_channel_rx);
    last_write_addr = dma_chan->write_addr;
    //uint32_t buf_end = (uint32_t)(read_buffer + sizeof(read_buffer));
}

void isosnoop_dma_setup(PIO pio, uint sm) {
    dma_channel_rx = dma_claim_unused_channel(false);
    if (dma_channel_rx < 0) {
        panic("No free dma channels");
    }

    for(int i=0; i<sizeof(read_buffer); i++) {
        read_buffer[i] = i;
    }

    dma_channel_config config_rx = dma_channel_get_default_config(dma_channel_rx);
    channel_config_set_transfer_data_size(&config_rx, DMA_SIZE_8);
    channel_config_set_read_increment(&config_rx, false);
    channel_config_set_write_increment(&config_rx, true);

    channel_config_set_dreq(&config_rx, pio_get_dreq(pio, sm, false));

    // enable dma in ring buffer mode with specified bit size
    channel_config_set_ring(&config_rx, true, ISOSNOOP_BUFFER_BITS);
    // configure dma to read from pio fifo
    dma_channel_configure(dma_channel_rx, &config_rx, read_buffer, (io_rw_8*)&pio->rxf[sm] + 0, dma_encode_endless_transfer_count(), true); // dma started
}

uint32_t request_word = 0;
uint8_t request_bit_counter = 0;
//uint32_t request_word_counter = 0;

uint8_t reply_word = 0;
uint8_t reply_bit_counter = 0;
uint32_t reply_word_counter = 0;

bool is_reply = false;
//int8_t current_bank = 0;

uint16_t cell_voltages[130] = {0};

void process_current_reply_word() {
    uint32_t left_aligned_request_word = (
        request_bit_counter < 32
        ? (request_word << (32 - request_bit_counter))
        : request_word
    );

    //printf("Request: %08x (%d bits)\n", left_aligned_request_word, request_bit_counter);

    // Work out the bank number from the command in the request word.
    int8_t bank = ((left_aligned_request_word >> 24) & 0xff) - 0x47;

    if(reply_word_counter>=1 && bank>=0 && bank<=4) {
        // We're reading in replies to a voltage request, for a valid bank.

        // A bank read consists of several groups of 9 bytes, each group
        // corresponding to a cell module (BMB). The first 6 bytes in each group
        // are the cell voltages (2 bytes per cell, for 3 cells). 

        // The first reply byte is extraneous padding which we skip over.

        // Determine which module this is for.
        int module = (reply_word_counter-1)/9;
        // And which byte within the module group this is (0-8)
        int word_in_module = (reply_word_counter-1)%9;
        if(word_in_module<6) {
            // Only the first six bytes are cell voltage data.
            // Work out which cell it is.
            int cell_in_module = word_in_module / 2;
            // And the logical cell index (across all banks/modules).
            int cell_index = (module * 15) + (bank * 3) + cell_in_module;

            if((word_in_module%2)==0) {
                // is the LSB
                cell_voltages[cell_index] = (reply_word & 0xff);
            } else {
                // is the MSB
                cell_voltages[cell_index] |= (reply_word << 8);
                if(cell_index==119) {
                    // Last cell in scan, print all voltages
                    for(int i=0;i<15;i++) {
                        printf("[c%3d]: %4d mV | ", i, (cell_voltages[i] * 2) / 25);
                        if(i%5==4) {
                            printf("\n");
                        }
                    }
                }
            }
        }
    }

    reply_word_counter++;
    reply_bit_counter = 0;
    reply_word = 0;
}

void isosnoop_print_buffer() {
    uint32_t write_addr = dma_chan->write_addr;

    while(last_write_addr != write_addr) {
        uint8_t b = *((uint8_t *)last_write_addr);
        // Increment last_write_addr, wrapping within the (aligned) buffer
        last_write_addr = 
            (last_write_addr & ~((1<<ISOSNOOP_BUFFER_BITS)-1))
            | ((last_write_addr + 1) & ((1<<ISOSNOOP_BUFFER_BITS)-1));
        for(int i=0;i<2;i++) {
            uint8_t chunk = b & 0xf0;
            b <<= 4;
            
            switch(chunk>>4) {
            case 0b1010:
                // CS1 (start or end of transfer)

                if(reply_bit_counter) {
                    // We have received some reply bits, process them.
                    process_current_reply_word();
                }

                // Reset the counters
                request_bit_counter = 0;
                request_word = 0;
                reply_word_counter = 0;
                reply_bit_counter = 0;
                reply_word = 0;

                // Next bit is not a reply.
                is_reply = false;
                break;
            case 0b0000:
                // Idle, therefore next bit is not a reply. This occurs on
                // requests that don't have a reply (they have this idle
                // instead). Or sometimes between CS and message bits.
                is_reply = false;
                break;
            case 0b0110:
                // Data 0
                if(is_reply) {
                    reply_word = (reply_word << 1) | 0;
                    reply_bit_counter++;
                } else if(request_bit_counter < 32) {
                    request_word = (request_word << 1) | 0;
                    request_bit_counter++;
                }
                // The message bits alternate between request/reply
                is_reply = !is_reply;
                break;
            case 0b1001:
                // Data 1
                if(is_reply) {
                    reply_word = (reply_word << 1) | 1;
                    reply_bit_counter++;
                } else if(request_bit_counter < 32) {
                    request_word = (request_word << 1) | 1;
                    request_bit_counter++;
                }
                // The message bits alternate between request/reply
                is_reply = !is_reply;
                break;
            }

            if(reply_bit_counter==8) {
                // We have received a full 8-bit word, process it.
                process_current_reply_word();
            }
        }
    }
}
