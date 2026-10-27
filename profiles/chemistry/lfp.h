#pragma once

// Base settings for LFP cells. Pack profiles include this and then add their
// own pack-specific settings (see profiles/README.md).

#include "chemistry/chemistry.h"

#define CHEMISTRY LFP

// TODO - derate voltage limits based on temperature

// VOLTAGE

// Hard voltage limits, beyond which the battery will be cut off
#define CELL_VOLTAGE_HARD_MIN_mV 2500
#define CELL_VOLTAGE_HARD_MAX_mV 3650
// Soft voltage limits, beyond which the battery will only allow a low-current
// restoration charge/discharge
#define DEFAULT_CELL_VOLTAGE_SOFT_MIN_mV 2800
#define DEFAULT_CELL_VOLTAGE_SOFT_MAX_mV 3400
// Working voltage range, which defines 0% and 100% SoC. This may be
// overridden by user settings.
#define DEFAULT_CELL_VOLTAGE_WORKING_MIN_mV 2900
#define DEFAULT_CELL_VOLTAGE_WORKING_MAX_mV 3350

#define CELL_VOLTAGE_DELTA_THRESHOLD_UP_mV 100
#define CELL_VOLTAGE_DELTA_THRESHOLD_DOWN_mV 50
#define CELL_VOLTAGE_DELTA_CURRENT_LIMIT_dA 10

// TODO - this is the NMC value, which is above CELL_VOLTAGE_HARD_MAX_mV, so LFP
// packs never balance unless the user setting overrides it.
#define DEFAULT_MINIMUM_BALANCING_VOLTAGE_mV 3770

// TEMPERATURE

// Temperatures at which charge power limit falls to zero
#define MAX_CHARGE_TEMPERATURE_LIMIT 50.0f
#define MIN_CHARGE_TEMPERATURE_LIMIT  0.0f
// Charge current-per-temp linear derating (near min and max limits)
#define CHARGE_TEMPERATURE_DERATE_A_PER_C 2.0f

// Temperatures at which discharge power limit falls to zero
#define MAX_DISCHARGE_TEMPERATURE_LIMIT  55.0f
#define MIN_DISCHARGE_TEMPERATURE_LIMIT -15.0f
// Discharge current-per-temp linear derating (near min and max limits)
#define DISCHARGE_TEMPERATURE_DERATE_A_PER_C 2.0f

// Hard temperature limits, beyond which the battery will be cut off
#define TEMPERATURE_HARD_MAX 60.0f
#define TEMPERATURE_HARD_MIN -20.0f
// Soft temperature limits, beyond which the battery will warn
#define TEMPERATURE_SOFT_MAX 55.0f
#define TEMPERATURE_SOFT_MIN -15.0f
