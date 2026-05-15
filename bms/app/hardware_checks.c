#include "../config/limits.h"
#include "model.h"
#include "../sys/events/events.h"

void confirm_supply_voltages(const supply_voltages_t *supply_voltages) {
    if(supply_voltages->voltage_contactor_millis==0) {
        // This is the last voltage read, so once set all others should be available
        return;
    }

    if(confirm(
        millis_recent_enough(supply_voltages->voltage_3V3_millis, SUPPLY_VOLTAGE_STALE_THRESHOLD_MS),
        ERR_SUPPLY_VOLTAGE_STALE,
        0x1000000000000000
    )) {
        confirm(
            supply_voltages->voltage_3V3_mV >= SUPPLY_VOLTAGE_3V3_MIN_MV,
            ERR_SUPPLY_VOLTAGE_3V3_LOW,
            supply_voltages->voltage_3V3_mV
        );
        confirm(
            supply_voltages->voltage_3V3_mV <= SUPPLY_VOLTAGE_3V3_MAX_MV,
            ERR_SUPPLY_VOLTAGE_3V3_HIGH,
            supply_voltages->voltage_3V3_mV
        );
    }

    if(confirm(
        millis_recent_enough(supply_voltages->voltage_5V_millis, SUPPLY_VOLTAGE_STALE_THRESHOLD_MS),
        ERR_SUPPLY_VOLTAGE_STALE,
        0x2000000000000000
    )) {
        confirm(
            supply_voltages->voltage_5V_mV >= SUPPLY_VOLTAGE_5V_MIN_MV,
            ERR_SUPPLY_VOLTAGE_5V_LOW,
            supply_voltages->voltage_5V_mV
        );
        confirm(
            supply_voltages->voltage_5V_mV <= SUPPLY_VOLTAGE_5V_MAX_MV,
            ERR_SUPPLY_VOLTAGE_5V_HIGH,
            supply_voltages->voltage_5V_mV
        );
    }

    if(confirm(
        millis_recent_enough(supply_voltages->voltage_12V_millis, SUPPLY_VOLTAGE_STALE_THRESHOLD_MS),
        ERR_SUPPLY_VOLTAGE_STALE,
        0x3000000000000000
    )) {
        confirm(
            supply_voltages->voltage_12V_mV >= SUPPLY_VOLTAGE_12V_MIN_MV,
            ERR_SUPPLY_VOLTAGE_12V_LOW,
            supply_voltages->voltage_12V_mV
        );
        confirm(
            supply_voltages->voltage_12V_mV <= SUPPLY_VOLTAGE_12V_MAX_MV,
            ERR_SUPPLY_VOLTAGE_12V_HIGH,
            supply_voltages->voltage_12V_mV
        );
    }

    if(confirm(
        millis_recent_enough(supply_voltages->voltage_contactor_millis, SUPPLY_VOLTAGE_STALE_THRESHOLD_MS),
        ERR_SUPPLY_VOLTAGE_STALE,
        0x4000000000000000
    )) {
        confirm(
            supply_voltages->voltage_contactor_mV >= SUPPLY_VOLTAGE_CONTACTOR_SOFT_MIN_MV,
            ERR_SUPPLY_VOLTAGE_CONTACTOR_LOW,
            supply_voltages->voltage_contactor_mV
        );
        confirm(
            supply_voltages->voltage_contactor_mV >= SUPPLY_VOLTAGE_CONTACTOR_HARD_MIN_MV,
            ERR_SUPPLY_VOLTAGE_CONTACTOR_VERY_LOW,
            supply_voltages->voltage_contactor_mV
        );
        confirm(
            supply_voltages->voltage_contactor_mV <= SUPPLY_VOLTAGE_CONTACTOR_MAX_MV,
            ERR_SUPPLY_VOLTAGE_CONTACTOR_HIGH,
            supply_voltages->voltage_contactor_mV
        );
    }
}

void confirm_hardware_integrity(const bms_model_t *model) {
    confirm_supply_voltages(&model->supply_voltages);
}
