#pragma once

// Base settings for NMC cells. Pack profiles include this and then add their
// own pack-specific settings (see profiles/README.md).

#include "chemistry/chemistry.h"

#define CHEMISTRY NMC

// TODO - derate voltage limits based on temperature

// VOLTAGE

// Hard voltage limits, beyond which the battery will be cut off
#define CELL_VOLTAGE_HARD_MIN_mV 2700
#define CELL_VOLTAGE_HARD_MAX_mV 4250
// Soft voltage limits, beyond which the battery will only allow a low-current
// restoration charge/discharge.
#define DEFAULT_CELL_VOLTAGE_SOFT_MIN_mV 3000
#define DEFAULT_CELL_VOLTAGE_SOFT_MAX_mV 4200
// Working voltage range, which defines 0% and 100% SoC. This may be
// overridden by user settings.
#define DEFAULT_CELL_VOLTAGE_WORKING_MIN_mV 3300
#define DEFAULT_CELL_VOLTAGE_WORKING_MAX_mV 4050

#define CELL_VOLTAGE_DELTA_THRESHOLD_UP_mV 100
#define CELL_VOLTAGE_DELTA_THRESHOLD_DOWN_mV 50
#define CELL_VOLTAGE_DELTA_CURRENT_LIMIT_dA 10

#define CHARGE_CELL_VOLTAGE_DERATE_dA_PER_SoC 40 // in 0.1A per percent SoC
#define DISCHARGE_CELL_VOLTAGE_DERATE_dA_PER_SoC 20 // in 0.1A per percent SoC

// Is voltage derating of current limits feasible, given the steepness of the
// voltage curves at top of charge? Probably not?

// So we stop at 4200mV. 90% SoC is about 4150mV, so we have about 50mV of
//
// TODO - do voltage-based-SoC-linearisation at the extremes (should be
// reasonably accurate) and limit by SoC

#define CHARGE_VOLTAGE_DERATE_dA_PER_mV 5
#define DISCHARGE_VOLTAGE_DERATE_dA_PER_mV 5

#define DEFAULT_MINIMUM_BALANCING_VOLTAGE_mV 3770
//3830

// TEMPERATURE

// Temperatures by which charge power limit falls to zero
#define MAX_CHARGE_TEMPERATURE_LIMIT  50.0f
#define MIN_CHARGE_TEMPERATURE_LIMIT   0.0f
// Charge current-per-temp linear derating (near min and max limits)
#define CHARGE_TEMPERATURE_DERATE_A_PER_C 2.0f

// Temperatures by which discharge power limit falls to zero
#define MAX_DISCHARGE_TEMPERATURE_LIMIT  55.0f
#define MIN_DISCHARGE_TEMPERATURE_LIMIT  -5.0f
// Discharge current-per-temp linear derating (near min and max limits)
#define DISCHARGE_TEMPERATURE_DERATE_A_PER_C 2.0f

#define TEMPERATURE_HARD_MAX  60.0f
#define TEMPERATURE_HARD_MIN -10.0f

#define TEMPERATURE_SOFT_MAX  50.0f
#define TEMPERATURE_SOFT_MIN   0.0f
