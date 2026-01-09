#include "../model.h"
#include "../limits.h"
#include "../debug/events.h"

void confirm_battery_safety(bms_model_t *model) {
    bool not_fully_initialized = (
       model->system_sm.state == SYSTEM_STATE_UNINITIALIZED ||
       model->system_sm.state == SYSTEM_STATE_INITIALIZING
    );

    if(check_or_confirm(
        millis_recent_enough(model->battery_voltage_millis, BATTERY_VOLTAGE_STALE_THRESHOLD_MS),
        // Don't raise faults if we're still initializing
        !not_fully_initialized,
        ERR_BATTERY_VOLTAGE_STALE,
        LEVEL_CRITICAL,
        0x1000000000000000
    )) {
        confirm(
            model->battery_voltage_mV <= BATTERY_VOLTAGE_SOFT_MAX_mV,
            ERR_BATTERY_VOLTAGE_HIGH,
            LEVEL_WARNING,
            model->battery_voltage_mV
        );
        confirm(
            model->battery_voltage_mV <= BATTERY_VOLTAGE_HARD_MAX_mV,
            ERR_BATTERY_VOLTAGE_VERY_HIGH,
            LEVEL_CRITICAL,
            model->battery_voltage_mV
        );
        confirm(
            model->battery_voltage_mV >= BATTERY_VOLTAGE_SOFT_MIN_mV,
            ERR_BATTERY_VOLTAGE_LOW,
            LEVEL_WARNING,
            model->battery_voltage_mV
        );
        confirm(
            model->battery_voltage_mV >= BATTERY_VOLTAGE_HARD_MIN_mV,
            ERR_BATTERY_VOLTAGE_VERY_LOW,
            LEVEL_CRITICAL,
            model->battery_voltage_mV
        );
    }

    if(check_or_confirm(
        millis_recent_enough(model->cell_voltage_millis, CELL_VOLTAGE_STALE_THRESHOLD_MS),
        // Don't raise faults if we're still initializing
        !not_fully_initialized,
        ERR_CELL_VOLTAGES_STALE,
        LEVEL_CRITICAL,
        0x1000000000000000
    )) {
        confirm(
            model->cell_voltage_max_mV <= CELL_VOLTAGE_SOFT_MAX_mV,
            ERR_CELL_VOLTAGE_HIGH,
            LEVEL_WARNING,
            model->cell_voltage_max_mV
        );
        confirm(
            model->cell_voltage_max_mV <= CELL_VOLTAGE_HARD_MAX_mV,
            ERR_CELL_VOLTAGE_VERY_HIGH,
            LEVEL_CRITICAL,
            model->cell_voltage_max_mV
        );
        confirm(
            model->cell_voltage_min_mV >= CELL_VOLTAGE_SOFT_MIN_mV,
            ERR_CELL_VOLTAGE_LOW,
            LEVEL_WARNING,
            model->cell_voltage_min_mV
        );
        confirm(
            model->cell_voltage_min_mV >= CELL_VOLTAGE_HARD_MIN_mV,
            ERR_CELL_VOLTAGE_VERY_LOW,
            LEVEL_CRITICAL,
            model->cell_voltage_min_mV
        );
    }



}