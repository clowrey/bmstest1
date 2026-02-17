#pragma once

#include "sys/logging/logging.h"

#include <math.h>
#include <stdbool.h>

/**
 * Update the AEMA filter with a new sample.
 * No separate initialization is required; the first call will set the initial value.
 *
 * @param value Pointer to the current filtered value (will be updated).
 * @param deviation Pointer to the current deviation/noise estimate (track Mean Absolute Deviation). Can be NULL.
 * @param input The raw sensor value.
 * @param alpha_min Minimal smoothing factor (0.0 to 1.0) for steady state.
 * @param alpha_max Maximum smoothing factor (0.0 to 1.0) for fast response.
 * @param noise_threshold Deviation considered "noise" (uses alpha_min).
 * @param change_threshold Deviation considered "real change" (uses alpha_max).
 * @return The updated filtered value.
 */
static inline float aema_update(float *value, float *deviation, float input, 
                                float alpha_min, float alpha_max, 
                                float noise_threshold, float change_threshold) {
    if (isnan(*value)) {
        *value = input;
        if (deviation) {
            *deviation = 0.0f;
        }
        return *value;
    }

   // Calculate absolute error between new input and current average
    float diff = fabsf(input - *value);
    
    // Calculate dynamic Alpha
    float alpha;
    
    if (diff <= noise_threshold) {
        // Error is small (noise): Use heavy smoothing
        alpha = alpha_min;
    } 
    else if (diff >= change_threshold) {
        // Error is large (signal change): React quickly
        //debug_printf("AEMA: input=%.3f, current=%.3f, diff=%.3f -> using alpha_max\n", input, *value, diff);
        alpha = alpha_max;
    } 
    else {
        // Error is intermediate: Linearly interpolate alpha
        float ratio = (diff - noise_threshold) / (change_threshold - noise_threshold);
        alpha = alpha_min + (ratio * (alpha_max - alpha_min));
    }
    
    //printf("alpha=%.5f, input=%.3f, old_value=%.3f, new_value=", alpha, input, *value);
    // Apply Standard EMA formula with the dynamic alpha
    *value = (alpha * input) + ((1.0f - alpha) * *value);

    if (deviation) {
        // Update Mean Absolute Deviation using the same alpha
        float deviation_alpha = 0.1f;
        *deviation = (deviation_alpha * diff) + ((1.0f - deviation_alpha) * *deviation);
    }

    //printf("%.3f\n", *value);
    
    return *value;
}
