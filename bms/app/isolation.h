#pragma once

#include <stdbool.h>
#include <stdint.h>

// Isolation monitor: ADS1115 channels 4/5 measure battery+ and battery- to
// chassis through the HV dividers, biased around a zero point (the ADC count
// both channels read when the pack floats with no leakage).

typedef struct {
    float pos_to_chassis_V;  // battery+ relative to chassis
    float neg_to_chassis_V;  // battery- relative to chassis (normally negative)
    float resistance_ohms;   // leakage estimate; NAN unless the contactors are open
} isolation_result_t;

// Zero point in ADC counts at board temperature temp_dC. zero_ref is the
// calibrated zero referred to ISOLATION_ZERO_CAL_TEMP_DC; 0 means "use the
// board default".
float isolation_zero_counts(float zero_ref, int32_t temp_dC);

// Converts a zero measured at temp_dC into the stored (temperature-referred) form.
float isolation_zero_ref_from_counts(float zero_counts, int32_t temp_dC);

// Converts the raw channel counts into pack-to-chassis voltages and, with the
// contactors open, a single-sided leakage estimate. The divider ratio is taken
// from the calibrated battery voltage and the channel span, so only the zero
// point needs calibrating. Returns all NAN if the inputs are unusable.
isolation_result_t isolation_compute(float pos_raw, float neg_raw, float zero,
                                     float battery_V, bool contactors_open);
