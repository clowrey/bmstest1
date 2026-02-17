#pragma once

#include "can2040.h"

void init_inverter_can(can2040_rx_cb rx_cb);
void inverter_can_transmit(struct can2040_msg *msg);
