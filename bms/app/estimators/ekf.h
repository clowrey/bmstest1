#pragma once

#include <math.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct bms_model bms_model_t;

// The filter runs in double precision. After days of coulomb counting on the
// LFP plateau the SoC and bias errors become almost perfectly correlated, and
// in float the rounding of the tiny per-second covariance increments pushes
// that correlation past 1. The next voltage update then makes the variances
// negative and the bias runs away (seen replaying a reboot onto the plateau).
typedef struct {
    // --- State Vector x ---
    // x[0] = Ah_used (Consumed Charge in Ah)
    // x[1] = V_c1 (Polarization Voltage in V)
    // x[2] = Capacity (Total Battery Capacity in Ah)
    // x[3] = Current sensor bias in A (measured = true + bias). Only weakly
    //        observable; it is learned mainly when the voltage pins the SoC at
    //        the top of charge, from how far the coulomb count has drifted.
    double x[4];

    // --- Covariance Matrix P (4x4) ---
    double P[4][4];

    // --- Tuning Parameters ---
    double Q[4];    // Process Noise Variances (Diagonal)
    double R;       // Measurement Noise Variance
    double min_ocv_slope; // Skip the voltage update where |dOCV/dSOC| (V per unit SOC) is below this...
    double knee_ocv_slope; // ...unless the measured voltage puts us on a part of the curve this steep

    // --- Model Parameters ---
    double R0;      // Ohmic Resistance
    double R1;      // Polarization Resistance
    double C1;      // Polarization Capacitance

    // --- Output Scaling Cache ---
    uint16_t prev_min_mV;
    uint16_t prev_max_mV;
    float prev_soc_min;
    float prev_soc_mul;

    bool initialized;
} ekf_t;

// Initialize the filter
void ekf_init(ekf_t *ekf, double initial_soc, double initial_capacity, double initial_bias_A);

// Run one iteration of the filter
// charge_Ah, current_amps: as measured (including any sensor bias).
// Positive = Charge, Negative = Discharge
// voltage_measured: Terminal voltage
void ekf_step(ekf_t *ekf, double charge_Ah, double current_amps, double voltage_measured);

// Helper to get current SOC
double ekf_get_soc(ekf_t *ekf);
void ekf_set_soc(bms_model_t *model, uint16_t soc);
// Forget the learned current bias, e.g. because the sensor offset was just recalibrated
void ekf_reset_bias(bms_model_t *model);

uint32_t ekf_tick(bms_model_t *model, float charge_Ah, int32_t current_mA, int32_t voltage_mV);

// Lookups on a 101-point OCV table (index = SOC in percent)
float table_soc(const float *table, float ocv);
double table_ocv(const float *table, double soc);

void ekf_print_state(ekf_t *ekf);
