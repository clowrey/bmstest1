#include "config/limits.h"
#include "app/model.h"

#include <stdint.h>
#include <stdio.h>

uint16_t calculate_cell_voltage_charge_current_limit(bms_model_t *model) {
    uint16_t charge_limit = 0xFFFF;

#if CHEMISTRY == LFP
    if(model->cell_voltage_max_mV >= 3570) {
        charge_limit = 0;
    } else if(model->cell_voltage_max_mV > 3350) {
        // The LFP knee is very sharp - use a quadratic curve to model it
        uint32_t delta_from_top = 3570 - model->cell_voltage_max_mV;
        charge_limit = (((71*CHARGE_MAX_CURRENT_dA)/500) * delta_from_top * delta_from_top + ((3014*CHARGE_MAX_CURRENT_dA)/500) * delta_from_top) / 8192;
    }
#elif CHEMISTRY == NMC
    if(model->cell_voltage_max_mV >= 4200) {
        charge_limit = 0;
    } else {
        // Linear derate using CHARGE_VOLTAGE_DERATE_dA_PER_mV
        int32_t delta_from_max_mV = 4200 - model->cell_voltage_max_mV;
        int32_t derate_dA = delta_from_max_mV * CHARGE_VOLTAGE_DERATE_dA_PER_mV;
        if(derate_dA >= 0) {
            charge_limit = derate_dA;
        }
    }
#endif

    if(model->cell_voltage_max_mV > get_cell_voltage_soft_max_mV(model)) {
        // Above max cell voltage, stop charging
        charge_limit = 0;
    }
    if(model->cell_voltage_min_mV < CELL_VOLTAGE_HARD_MIN_mV) {
        // Below hard min cell voltage, stop charging
        charge_limit = 0;
    }
    if(model->cell_voltage_min_mV < get_cell_voltage_soft_min_mV(model)) {
        // Below min cell voltage, limit charge current
        if(charge_limit > OVERDISCHARGE_CHARGE_CURRENT_LIMIT_dA) {
            charge_limit = OVERDISCHARGE_CHARGE_CURRENT_LIMIT_dA;
        }
    }

    // Working range limits
    int32_t guessed_ocv_uV = model->cell_voltage_max_mV*1000 - (model->current_mA * WORKING_LIMIT_INTERNAL_RESISTANCE_uR) / 1000;
    if(guessed_ocv_uV > (get_cell_voltage_working_max_mV(model) * 1000)) {
        charge_limit = 0;
    } else {
        int32_t delta_from_working_max_uV = (get_cell_voltage_working_max_mV(model) * 1000) - guessed_ocv_uV;
        int32_t max_delta_uV = (CHARGE_MAX_CURRENT_dA * WORKING_LIMIT_INTERNAL_RESISTANCE_uR) / 10;
        //printf("charge %ld %ld\n", delta_from_working_max_uV, max_delta_uV);
        // Derate from max down to zero as guessed OCV approaches the working max
        int32_t derate_dA = (delta_from_working_max_uV * CHARGE_MAX_CURRENT_dA) / max_delta_uV;
        if(derate_dA >= 0 && derate_dA < charge_limit) {
            charge_limit = derate_dA;
        }
    }

    return charge_limit;
}

uint16_t calculate_cell_voltage_discharge_current_limit(bms_model_t *model) {
    uint16_t discharge_limit = 0xFFFF;

#if CHEMISTRY == LFP
    if(model->cell_voltage_min_mV <= 2770) {
        discharge_limit = 0;
    } else if(model->cell_voltage_min_mV < 3300) {
        int32_t delta_from_min_mV = model->cell_voltage_min_mV - 2700;
        int32_t numerator = delta_from_min_mV * (((1545*DISCHARGE_MAX_CURRENT_dA)/500) * delta_from_min_mV - ((112537*DISCHARGE_MAX_CURRENT_dA)/500));

        if (numerator < 0) {
            discharge_limit = 0;
        } else {
            discharge_limit = numerator >> 19;
        }
    }
#elif CHEMISTRY == NMC
    if(model->cell_voltage_min_mV <= 2900) {
        discharge_limit = 0;
    } else {
        // Linear derate using DISCHARGE_VOLTAGE_DERATE_dA_PER_mV
        int32_t delta_from_min_mV = model->cell_voltage_min_mV - 2900;
        int32_t derate_dA = delta_from_min_mV * DISCHARGE_VOLTAGE_DERATE_dA_PER_mV;
        if(derate_dA >= 0) {
            discharge_limit = derate_dA;
        }
    }
#endif

    if(model->cell_voltage_min_mV < get_cell_voltage_soft_min_mV(model)) {
        // Hit min cell voltage, stop discharging
        discharge_limit = 0;
    }
    if(model->cell_voltage_max_mV > get_cell_voltage_soft_max_mV(model)) {
        // Above max cell voltage, limit discharge current
        if(discharge_limit > OVERCHARGE_DISCHARGE_CURRENT_LIMIT_dA) {
            discharge_limit = OVERCHARGE_DISCHARGE_CURRENT_LIMIT_dA;
        }
    }

    // Working range limits
    int32_t guessed_ocv_uV = model->cell_voltage_min_mV*1000 - (model->current_mA * WORKING_LIMIT_INTERNAL_RESISTANCE_uR) / 1000;
    if(guessed_ocv_uV < (get_cell_voltage_working_min_mV(model) * 1000)) {
        discharge_limit = 0;
    } else {
        int32_t delta_from_working_min_uV = guessed_ocv_uV - (get_cell_voltage_working_min_mV(model) * 1000);
        int32_t max_delta_uV = (DISCHARGE_MAX_CURRENT_dA * WORKING_LIMIT_INTERNAL_RESISTANCE_uR) / 10;
        //printf("discharge %ld %ld\n", delta_from_working_min_uV, max_delta_uV);
        // Derate from max down to zero as guessed OCV approaches the working min
        int32_t derate_dA = (delta_from_working_min_uV * DISCHARGE_MAX_CURRENT_dA) / max_delta_uV;
        if(derate_dA >= 0 && derate_dA < discharge_limit) {
            discharge_limit = derate_dA;
        }
    }

    return discharge_limit;
}

uint16_t calculate_temperature_charge_current_limit(int16_t temperature_min_dC, int16_t temperature_max_dC) {
    if(temperature_max_dC >= MAX_CHARGE_TEMPERATURE_LIMIT_dC || temperature_min_dC <= MIN_CHARGE_TEMPERATURE_LIMIT_dC) {
        return 0;
    } else {
        uint16_t upper_charge_limit = (MAX_CHARGE_TEMPERATURE_LIMIT_dC - temperature_max_dC) * CHARGE_TEMPERATURE_DERATE_dA_PER_dC;
        uint16_t lower_charge_limit = (temperature_min_dC - MIN_CHARGE_TEMPERATURE_LIMIT_dC) * CHARGE_TEMPERATURE_DERATE_dA_PER_dC;
        return (upper_charge_limit < lower_charge_limit) ? upper_charge_limit : lower_charge_limit;
    }
    return 0xFFFF;
}

uint16_t calculate_temperature_discharge_current_limit(int16_t temperature_min_dC, int16_t temperature_max_dC) {
    if(temperature_max_dC >= MAX_DISCHARGE_TEMPERATURE_LIMIT_dC || temperature_min_dC <= MIN_DISCHARGE_TEMPERATURE_LIMIT_dC) {
        return 0;
    } else {
        uint16_t upper_discharge_limit = (MAX_DISCHARGE_TEMPERATURE_LIMIT_dC - temperature_max_dC) * DISCHARGE_TEMPERATURE_DERATE_dA_PER_dC;
        uint16_t lower_discharge_limit = (temperature_min_dC - MIN_DISCHARGE_TEMPERATURE_LIMIT_dC) * DISCHARGE_TEMPERATURE_DERATE_dA_PER_dC;
        return (upper_discharge_limit < lower_discharge_limit) ? upper_discharge_limit : lower_discharge_limit;
    }
    return 0xFFFF;
}

