#pragma once

#include <math.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct bms_model bms_model_t;

typedef struct {
    // --- State Vector x ---
    // x[0] = Ah_used (Consumed Charge in Ah)
    // x[1] = V_c1 (Polarization Voltage in V)
    // x[2] = Capacity (Total Battery Capacity in Ah)
    // x[3] = Current sensor bias in A (measured = true + bias). Only weakly
    //        observable; it is learned mainly when the voltage pins the SoC at
    //        the top of charge, from how far the coulomb count has drifted.
    float x[4];

    // --- Covariance Matrix P (4x4) ---
    float P[4][4];

    // --- Tuning Parameters ---
    float Q[4];    // Process Noise Variances (Diagonal)
    float R;       // Measurement Noise Variance
    float min_ocv_slope; // Skip the voltage update where |dOCV/dSOC| (V per unit SOC) is below this...
    float knee_ocv_slope; // ...unless the measured voltage puts us on a part of the curve this steep
    
    // --- Model Parameters ---
    float R0;      // Ohmic Resistance
    float R1;      // Polarization Resistance
    float C1;      // Polarization Capacitance

    // --- Output Scaling Cache ---
    uint16_t prev_min_mV;
    uint16_t prev_max_mV;
    float prev_soc_min;
    float prev_soc_mul;

    bool initialized;
} ekf_t;

// Initialize the filter
void ekf_init(ekf_t *ekf, float initial_soc, float initial_capacity, float initial_bias_A);

// Run one iteration of the filter
// charge_Ah, current_amps: as measured (including any sensor bias).
// Positive = Charge, Negative = Discharge
// voltage_measured: Terminal voltage
void ekf_step(ekf_t *ekf, float charge_Ah, float current_amps, float voltage_measured);

// Helper to get current SOC
float ekf_get_soc(ekf_t *ekf);
void ekf_set_soc(bms_model_t *model, uint16_t soc);
// Forget the learned current bias, e.g. because the sensor offset was just recalibrated
void ekf_reset_bias(bms_model_t *model);

uint32_t ekf_tick(bms_model_t *model, float charge_Ah, int32_t current_mA, int32_t voltage_mV);

void ekf_print_state(ekf_t *ekf);
