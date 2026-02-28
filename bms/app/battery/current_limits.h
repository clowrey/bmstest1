#pragma once

#include <stdint.h>
#include "../model.h"

uint16_t calculate_cell_voltage_charge_current_limit(bms_model_t *model);
uint16_t calculate_cell_voltage_discharge_current_limit(bms_model_t *model);
uint16_t calculate_temperature_charge_current_limit(int16_t temperature_min_dC, int16_t temperature_max_dC);
uint16_t calculate_temperature_discharge_current_limit(int16_t temperature_min_dC, int16_t temperature_max_dC);
uint16_t calculate_working_charge_current_limit(bms_model_t *model);