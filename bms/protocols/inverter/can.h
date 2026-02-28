#pragma once

#include "can2040.h"

void init_inverter_can(can2040_rx_cb rx_cb);
int inverter_can_transmit(const struct can2040_msg *msg);
