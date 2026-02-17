#include "app/model.h"
#include "can2040.h"
#include "pico/unique_id.h"
#include "sys/logging/logging.h"
#include "protocols/inverter/inverter.h"
#include "protocols/inverter/can.h"
#include <string.h>

#define CUSTOM_CAN_BROADCAST_ID     0x0c311000
#define CUSTOM_CAN_METERS_ID_BASE   0x0c311100
#define CUSTOM_CAN_LIMITS_ID_BASE   0x0c311200
#define CUSTOM_CAN_TEMPS_ID_BASE    0x0c311300
#define CUSTOM_CAN_CELLVOLT_ID_BASE 0x0c311400
// states (fault, contactors, etc)

static uint64_t serial_fragment;
static uint8_t inverter_addr;

static void can2040_cb(struct can2040 *cd, uint32_t notify, struct can2040_msg *msg) {
    if (notify != CAN2040_NOTIFY_RX) return;

    bool is_eff = (msg->id & CAN2040_ID_EFF) != 0;
    uint32_t id = msg->id & 0x1FFFFFFF;

    // Check for pairing response: Broadcast ID with matching 7-byte serial fragment
    if (is_eff && id == CUSTOM_CAN_BROADCAST_ID && msg->dlc == 8) {
        uint64_t rx_serial = 0;
        memcpy(&rx_serial, &msg->data[1], 7);
        if (rx_serial == serial_fragment) {
            inverter_addr = msg->data[0];
            info_printf("Custom CAN: Paired with address 0x%02X\n", inverter_addr);
        }
    }
}

void init_inverter() {
    pico_unique_board_id_t id;
    pico_get_unique_board_id(&id);
    // Use first 7 bytes of the unique ID as the fragment
    serial_fragment = 0;
    memcpy(&serial_fragment, &id.id[0], 7);

    asdf

    init_inverter_can(can2040_cb);
    info_printf("Custom CAN: Initialized, serial fragment 0x%llX\n", serial_fragment);
}

void inverter_tick(bms_model_t *model) {
    // Send messages every 100ms (5 timesteps @ 20ms)
    if (timestep() % 5 != 0) {
        return;
    }

    // 1. Always send the broadcast frame
    struct can2040_msg bcast_msg = {0};
    bcast_msg.id = CAN2040_ID_EFF | CUSTOM_CAN_BROADCAST_ID;
    bcast_msg.dlc = 8;
    bcast_msg.data[0] = inverter_addr;
    memcpy(bcast_msg.data + 1, &serial_fragment, 7);
    inverter_can_transmit(&bcast_msg);

    // 2. If paired, also send the status frame
    if (inverter_addr > 0) {
        struct can2040_msg status_msg = {0};
        status_msg.id = CAN2040_ID_EFF | CUSTOM_CAN_METERS_ID_BASE | inverter_addr;
        status_msg.dlc = 8;
        
        // Pack data (big endian)
        // Byte 0-1: Voltage (mV)
        uint16_t v_mV = (uint16_t)(model->battery_voltage * 1000.0f);
        status_msg.data[0] = (v_mV >> 8) & 0xFF;
        status_msg.data[1] = v_mV & 0xFF;
        
        // Byte 2-5: Current (mA)
        status_msg.data[2] = (model->current_mA >> 24) & 0xFF;
        status_msg.data[3] = (model->current_mA >> 16) & 0xFF;
        status_msg.data[4] = (model->current_mA >> 8) & 0xFF;
        status_msg.data[5] = model->current_mA & 0xFF;
        
        // Byte 6-7: SoC (0.01% units)
        status_msg.data[6] = (model->soc >> 8) & 0xFF;
        status_msg.data[7] = model->soc & 0xFF;

        inverter_can_transmit(&status_msg);
    }
}
