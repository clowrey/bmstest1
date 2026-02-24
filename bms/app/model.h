#pragma once

#include "sys/time/time.h"
#include "app/calibration/offline.h"
#include "app/calibration/online.h"
#include "app/battery/balancing.h"
#include "app/state_machines/contactors.h"
#include "app/state_machines/system.h"
#include "app/estimators/ekf.h"
#include "config/limits.h"

#include <stdint.h>
#include <stdio.h>

// Persistent data (saved to NVM)
// Do not reorder the fields without updating the version number. New fields can
// be added to the end (they will default to zero);

// 'Fast' persistent data (saved frequently)

typedef struct {
    uint32_t nameplate_capacity_mC; // nameplate battery capacity in mC
    float charge_used_Ah; // charge used in Ah
    bool operating; // Whether BMS should resume operating if restarted
} bms_model_persistent_fast_t;

static const uint32_t BMS_MODEL_PERSISTENT_FAST_VERSION = 1;

// 'Slow' persistent data (saved infrequently)

typedef struct {
    // ADS1115 voltage calibration
    float battery_voltage_mul;
    float output_voltage_mul;
    float neg_contactor_mul;
    float neg_contactor_offset_mV;
    float pos_contactor_mul;

    // Current calibration
    int32_t current_offset;
} calibration_data_t;

typedef struct {
    calibration_data_t;

    uint16_t user_charge_current_limit_dA; // in 0.1A units
    uint16_t user_discharge_current_limit_dA; // in 0.1A units

    uint16_t cell_voltage_soft_min_mV;
    uint16_t cell_voltage_soft_max_mV;

    // User-configured working voltage range (zero for defaults)
    uint16_t cell_voltage_working_min_mV;
    uint16_t cell_voltage_working_max_mV;

    // Inverter voltage limit offsets (to account for discrepancies between BMS and inverter readings)
    // These values will increase the voltage limits sent to the inverter
    int16_t pack_voltage_limit_upper_offset_dV; // in 0.1V units
    int16_t pack_voltage_limit_lower_offset_dV; // in 0.1V units

    // Inverter SoC scaling
    int16_t soc_scaling_min; // in 0.01% units
    int16_t soc_scaling_max; // in 0.01% units

    // Balancing configuration
    uint32_t auto_balancing_period_ms; // How long to wait between auto-balancing sessions. Zero to disable balancing.
    uint16_t balancing_periods_per_mV; // How many balancing periods per mV
    uint16_t balance_min_offset_mV; // Minimum voltage difference to balance
    uint16_t minimum_balancing_voltage_mV; // Minimum cell voltage to allow balancing

} bms_model_persistent_slow_t;

static const uint32_t BMS_MODEL_PERSISTENT_SLOW_VERSION = 2;

// This entire structure will be zero-initialized at startup
typedef struct bms_model {
    // Positive current means battery is charging
    int32_t current_mA;
    millis_t current_millis;
    int64_t charge_raw;
    millis_t charge_millis;

    int16_t temperature_min_dC;
    int16_t temperature_max_dC;
    millis_t temperature_millis;

    float battery_voltage;
    millis_t battery_voltage_millis;
    float battery_voltage_deviation;

    float output_voltage;
    millis_t output_voltage_millis;
    float output_voltage_deviation;

    float pos_contactor_voltage;
    millis_t pos_contactor_voltage_millis;
    float pos_contactor_voltage_deviation;

    float neg_contactor_voltage;
    millis_t neg_contactor_voltage_millis;
    float neg_contactor_voltage_deviation;

    uint16_t soc; // state of charge in 0.01% units (0=0%, 10000=100.00%)
    millis_t soc_millis;
    uint16_t soh; // state of health in 0.01% units (0=0%, 10000=100.00%)

    uint16_t soc_voltage_based;
    uint16_t soc_basic_count;
    uint16_t soc_fancy_count;

    union {
        bms_model_persistent_fast_t persistent_fast;
        bms_model_persistent_fast_t;
    };
    union {
        bms_model_persistent_slow_t persistent_slow;
        bms_model_persistent_slow_t;
    };

    uint32_t nvm_fast_saved_timestep;

    //uint32_t nameplate_capacity_mC; // nameplate battery capacity in mC
    uint32_t working_capacity_mC; // nameplate capacity within working voltage range in mC

    system_sm_t system_sm;
    system_requests_t system_req;

    contactors_sm_t contactor_sm;
    contactors_requests_t contactor_req;

    balancing_sm_t balancing_sm;

    offline_calibration_sm_t offline_calibration_sm;
    offline_calibration_requests_t offline_calibration_req;

    online_calibration_sm_t online_calibration_sm;
    online_calibration_requests_t online_calibration_req;

    // BATTERY DATA

    int16_t module_temperatures_dC[8];
    millis_t module_temperatures_millis;

    ekf_t ekf;

    // Aggregated cell voltage data
    int16_t cell_voltage_min_mV;
    int16_t cell_voltage_max_mV;
    int32_t cell_voltage_total_mV;
    millis_t cell_voltage_millis;
    
    // Individual cell voltages, which will remain static during balancing
    int16_t cell_voltages_mV[120];
    millis_t cell_voltages_millis; // individial cell voltages
    // Individual raw cell voltages, which will bounce around during balancing
    int16_t raw_cell_voltages_mV[120]; // are unstable during balancing
    millis_t raw_cell_voltages_millis;

    uint16_t raw_temperatures[16+24+8];

    bool cell_voltage_slow_mode; // only request BMB data infrequently

    // Inverter voltage limits (sent to the inverter, for inverters that allow
    // absorption charging to continue at the limit). Some inverters may ignore
    // these.
    int16_t inverter_min_voltage_limit_dV; // in 0.1V units
    int16_t inverter_max_voltage_limit_dV; // in 0.1V units
    int16_t inverter_soc; // in 0.01% units
    int16_t inverter_full_capacity_dAh;
    int16_t inverter_remaining_capacity_dAh;

    // Current limits (the lower of these limits will be used)
    uint16_t temp_charge_current_limit_dA; // in 0.1A units
    uint16_t temp_discharge_current_limit_dA; // in 0.1A units
    uint16_t pack_voltage_charge_current_limit_dA; // in 0.1A units
    uint16_t pack_voltage_discharge_current_limit_dA; // in 0.1A units
    uint16_t cell_voltage_charge_current_limit_dA; // in 0.1A units
    uint16_t cell_voltage_discharge_current_limit_dA; // in 0.1A units
    uint16_t working_charge_current_limit_dA; // in 0.1A units
    float working_charge_current_limit_filtered_dA;
    // The calculated final current limits
    uint16_t charge_current_limit_dA; // in 0.1A units
    uint16_t discharge_current_limit_dA; // in 0.1A units

    bool below_working_min; // Hysteresis flag

    // The amount of charge that has passed into the battery in excess of the
    // charge/discharge limits in the soft-limit region (slightly under or
    // overcharged). If this gets too large, we can cut off the battery to avoid
    // the problem worsening. We need a buffer to give some leeway during
    // inverter startup.
    int32_t soft_limit_charge_buffer_dC; // in 0.1C units

    // The amount of charge that has gone into/out of the battery beyond the
    // current limits.
    int32_t excess_charge_buffer_dC; // in 0.1C units
    int32_t excess_discharge_buffer_dC; // in 0.1C units

    int32_t supply_voltage_3V3_mV;
    millis_t supply_voltage_3V3_millis;
    int32_t supply_voltage_5V_mV;
    millis_t supply_voltage_5V_millis;
    int32_t supply_voltage_12V_mV;
    millis_t supply_voltage_12V_millis;
    int32_t supply_voltage_contactor_mV;
    millis_t supply_voltage_contactor_millis;

    // Calibration constants
    // int32_t battery_voltage_mul; // convert raw ADC to mV (/4096)
    // int32_t output_voltage_mul; // convert raw ADC to mV (/4096)
    // int32_t neg_contactor_mul;
    // int32_t neg_contactor_offset_mV;
    // int32_t pos_contactor_mul; // actually only the bat+ to out- part
    //int32_t current_offset;

    bool balancing_active; // whether balancing was requested during the past BMB cycle (and so whether any read voltages are unstable)
    int16_t balancing_voltage_threshold_mV; // Only balance cells above this voltage

    bool estop_pressed;
    // Detected via the aux contacts (note that this is actually the precharge bypass contactor)
    bool precharge_closed; 
    // Whether we should ignore a potential loop overrun at the end of this tick
    // (eg, due to a slow flash write). This is reset each tick.
    bool ignore_missed_deadline;
  
} bms_model_t;

extern bms_model_t model;

void model_tick(bms_model_t *model);

inline uint16_t get_cell_voltage_soft_min_mV(bms_model_t *model) {
    return (model->cell_voltage_soft_min_mV > 0) ? model->cell_voltage_soft_min_mV : DEFAULT_CELL_VOLTAGE_SOFT_MIN_mV;
}

inline uint16_t get_cell_voltage_soft_max_mV(bms_model_t *model) {
    return (model->cell_voltage_soft_max_mV > 0) ? model->cell_voltage_soft_max_mV : DEFAULT_CELL_VOLTAGE_SOFT_MAX_mV;
}

inline uint16_t get_cell_voltage_working_min_mV(bms_model_t *model) {
    return (model->cell_voltage_working_min_mV > 0) ? model->cell_voltage_working_min_mV : DEFAULT_CELL_VOLTAGE_WORKING_MIN_mV;
}

inline uint16_t get_cell_voltage_working_max_mV(bms_model_t *model) {
    return (model->cell_voltage_working_max_mV > 0) ? model->cell_voltage_working_max_mV : DEFAULT_CELL_VOLTAGE_WORKING_MAX_mV;
}

inline uint16_t get_minimum_balancing_voltage_mV(bms_model_t *model) {
    return (model->minimum_balancing_voltage_mV > 0) ? model->minimum_balancing_voltage_mV : DEFAULT_MINIMUM_BALANCING_VOLTAGE_mV;
}
