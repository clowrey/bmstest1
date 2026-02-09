#include "model.h"

#include "battery/current_limits.h"
#include "../config/limits.h"
#include "../lib/math.h"

bms_model_t model = {0};

static void model_process_temperatures(bms_model_t *model) {
    model->temperature_min_dC = model->module_temperatures_dC[0];
    model->temperature_max_dC = model->module_temperatures_dC[0];
    for(int i=1; i<NUM_MODULE_TEMPS; i++) {
        int16_t temp = model->module_temperatures_dC[i];
        if(temp < model->temperature_min_dC) {
            model->temperature_min_dC = temp;
        }
        if(temp > model->temperature_max_dC) {
            model->temperature_max_dC = temp;
        }   
    }
    model->temperature_millis = model->module_temperatures_millis;
}

static void model_process_cell_voltages(bms_model_t *model) {
    if(model->cell_voltages_millis == 0) {
        // No valid data yet
        return;
    }
    model->cell_voltage_min_mV = model->cell_voltages_mV[0];
    model->cell_voltage_max_mV = model->cell_voltages_mV[0];
    model->cell_voltage_total_mV = model->cell_voltages_mV[0];
    for(int i=1; i<NUM_CELLS; i++) {
        int32_t volt = model->cell_voltages_mV[i];

        // TODO - decide on how to handle missing cells
        if(volt < 0) {
            continue;
        }

        model->cell_voltage_total_mV += volt;

        if(volt < model->cell_voltage_min_mV) {
            model->cell_voltage_min_mV = volt;
        }
        if(volt > model->cell_voltage_max_mV) {
            model->cell_voltage_max_mV = volt;
        }
    }
    model->cell_voltage_millis = model->cell_voltages_millis;
}

static void model_calculate_cell_current_limits(bms_model_t *model) {
    model->cell_voltage_charge_current_limit_dA =calculate_cell_voltage_charge_current_limit(
        model
    );
    model->cell_voltage_discharge_current_limit_dA = calculate_cell_voltage_discharge_current_limit(
        model
    );
}

static void model_apply_current_limits(bms_model_t *model) {
    uint16_t charge_limit = CHARGE_MAX_CURRENT_dA;
    uint16_t discharge_limit = DISCHARGE_MAX_CURRENT_dA;

    if(!model->contactor_sm.enable_current) {
        // Contactor state machine disallows current flow
        charge_limit = 0;
        discharge_limit = 0;
    }

    // Temperature limits
    if(charge_limit > model->temp_charge_current_limit_dA) {
        charge_limit = model->temp_charge_current_limit_dA;
    }
    if(discharge_limit > model->temp_discharge_current_limit_dA) {
        discharge_limit = model->temp_discharge_current_limit_dA;
    }

    // Pack voltage limits
    // if(charge_limit > model->pack_voltage_charge_current_limit_dA) {
    //     charge_limit = model->pack_voltage_charge_current_limit_dA;
    // }
    // if(discharge_limit > model->pack_voltage_discharge_current_limit_dA) {
    //     discharge_limit = model->pack_voltage_discharge_current_limit_dA;
    // }

    // Cell voltage limits
    if(charge_limit > model->cell_voltage_charge_current_limit_dA) {
        charge_limit = model->cell_voltage_charge_current_limit_dA;
    }
    if(discharge_limit > model->cell_voltage_discharge_current_limit_dA) {
        discharge_limit = model->cell_voltage_discharge_current_limit_dA;
    }

    // User limits
    // if(charge_limit > model->user_charge_current_limit_dA) {
    //     charge_limit = model->user_charge_current_limit_dA;
    // }
    // if(discharge_limit > model->user_discharge_current_limit_dA) {
    //     discharge_limit = model->user_discharge_current_limit_dA;
    // }

    model->charge_current_limit_dA = charge_limit;
    model->discharge_current_limit_dA = discharge_limit;
}

// Check if the current exceeds the limits, and accumulate excess
// charge/discharge into separate buffers, so that we can cut off the battery if
// it goes on for too long. This is to protect the battery from excessively high
// charge or discharge currents.
static void model_accumulate_overcurrent(bms_model_t *model) {
    if(model->current_mA > 0) {
        // We are charging. Work out the excess current above the limit.
        int32_t excess_dA = (model->current_mA / 100) - model->charge_current_limit_dA - CURRENT_LIMIT_ERROR_MARGIN_dA;
        // Accumulate into the charge buffer if there is excess
        model->excess_charge_buffer_dC = sadd_i32(model->excess_charge_buffer_dC, 
                                                  (excess_dA > 0) ? excess_dA : 0);
    } else if(model->current_mA < 0) {
        // Discharging
        int32_t excess_dA = (-model->current_mA / 100) - model->discharge_current_limit_dA - CURRENT_LIMIT_ERROR_MARGIN_dA;
        // Accumulate into the discharge buffer if there is excess
        model->excess_discharge_buffer_dC = sadd_i32(model->excess_discharge_buffer_dC,
                                                     (excess_dA > 0) ? excess_dA : 0);
    }

    if(model->excess_charge_buffer_dC > 0) {
        // Decay the excess charge buffer slowly
        model->excess_charge_buffer_dC--;
    }
    if(model->excess_discharge_buffer_dC > 0) {
        // Decay the excess discharge buffer slowly
        model->excess_discharge_buffer_dC--;
    }
}

// Check if we are in the soft limit region, and accumulate excess
// charge/discharge into a single buffer. We can then tolerate a limited amount
// of charge/discharge (eg, whilst the inverter starts up), but can disconnect
// the battery if it continues for too long. This is to protect the battery from
// overcharge/overdischarge.
static void model_accumulate_soft_limit_overcurrent(bms_model_t *model) {
    if(model->cell_voltage_max_mV < get_cell_voltage_soft_max_mV(model) &&
       model->cell_voltage_min_mV > get_cell_voltage_soft_min_mV(model)) {
        // Not in soft limit region, reset buffer
        model->soft_limit_charge_buffer_dC = 0;
        return;
    }

    if(model->current_mA > 0) {
        // Charging
        int32_t excess_dA = (model->current_mA / 100) - model->charge_current_limit_dA;
        model->soft_limit_charge_buffer_dC = sadd_i32(model->soft_limit_charge_buffer_dC,
                                                      (excess_dA > 0) ? excess_dA : 0);
    } else if(model->current_mA < 0) {
        // Discharging
        int32_t excess_dA = (-model->current_mA / 100) - model->discharge_current_limit_dA;
        model->soft_limit_charge_buffer_dC = ssub_i32(model->soft_limit_charge_buffer_dC,
                                                        (excess_dA > 0) ? excess_dA : 0);
    }
}

static void model_check_overcurrent_accumulation(bms_model_t *model) {
    // TODO - do we actually want to cut off the battery, or just raise events?

    if(model->excess_charge_buffer_dC > OVERCURRENT_BUFFER_LIMIT_dC) {
        // Too much excess charge, cut off charging
    } else if(model->excess_discharge_buffer_dC > OVERCURRENT_BUFFER_LIMIT_dC) {
        // Too much excess discharge, cut off discharging
    }
}

static void model_calculate_temperature_current_limits(bms_model_t *model) {
    model->temp_charge_current_limit_dA = calculate_temperature_charge_current_limit(
        model->temperature_min_dC,
        model->temperature_max_dC
    );
    model->temp_discharge_current_limit_dA = calculate_temperature_discharge_current_limit(
        model->temperature_min_dC,
        model->temperature_max_dC
    );
}

static void model_calculate_inverter_voltage_limits(bms_model_t *model) {
    // For inverters that allow absorption charging to continue at the voltage
    // limit.

    uint16_t cell_voltage_working_max_mV = get_cell_voltage_working_max_mV(model);
    uint16_t cell_voltage_working_min_mV = get_cell_voltage_working_min_mV(model);

    uint32_t max_voltage_limit_dV = (cell_voltage_working_max_mV * NUM_CELLS) / 100; // in 0.1V units
    uint32_t min_voltage_limit_dV = (cell_voltage_working_min_mV * NUM_CELLS) / 100; // in 0.1V units
    
    int32_t mean_cell_voltage_mV = model->cell_voltage_total_mV / NUM_CELLS;

    // How far is the highest cell above the mean?
    int32_t deviation_max_mV = model->cell_voltage_max_mV - mean_cell_voltage_mV;
    // Calculate how much to reduce the voltage limit by to account for this
    // deviation, to avoid overcharging the highest cell.
    int32_t deviation_reduction_mV = deviation_max_mV * NUM_CELLS;
    if(deviation_reduction_mV > 0) {
        max_voltage_limit_dV -= deviation_reduction_mV / 100;
    }

    // How far is the lowest cell below the mean?
    int32_t deviation_min_mV = mean_cell_voltage_mV - model->cell_voltage_min_mV;
    // Calculate how much to increase the voltage limit by to account for this
    // deviation, to avoid overdischarging the lowest cell.
    int32_t deviation_increase_mV = deviation_min_mV * NUM_CELLS;
    if(deviation_increase_mV > 0) {
        min_voltage_limit_dV += deviation_increase_mV / 100;
    }

    // Apply user-configured offsets to account for errors in the inverter
    // voltage reading.
    max_voltage_limit_dV += model->pack_voltage_limit_upper_offset_dV;
    min_voltage_limit_dV += model->pack_voltage_limit_lower_offset_dV;  

    model->inverter_max_voltage_limit_dV = max_voltage_limit_dV;
    model->inverter_min_voltage_limit_dV = min_voltage_limit_dV;
}


void model_tick(bms_model_t *model) {
    model_process_temperatures(model);
    model_process_cell_voltages(model);

    model_calculate_cell_current_limits(model);
    model_calculate_temperature_current_limits(model);
    model_calculate_inverter_voltage_limits(model);

    model_apply_current_limits(model);
    model_accumulate_overcurrent(model);
    model_accumulate_soft_limit_overcurrent(model);

    model_check_overcurrent_accumulation(model);
}
