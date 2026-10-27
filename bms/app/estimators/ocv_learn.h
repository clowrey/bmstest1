#pragma once

#include <stdbool.h>
#include <stdint.h>

// Learns a correction to the OCV table from near-rest voltages, against Ah below
// settled full charge, and reads off the Ah from full down to OCV_LEARN_V_REF.
// Readings are held until the next full charge, so their Ah below full can be
// interpolated between the two (which also cancels any constant current offset).
// It never uses the EKF's own SOC, so errors can't feed back into it.

#define OCV_LEARN_BINS 13         // corrections evenly spaced from full to the factory curve's 0%
#define OCV_LEARN_MAX_READINGS 256 // one every 2 hours for 21 days
#define OCV_LEARN_FULL_SETTLE_S 600 // near the charge voltage and quiet this long counts as full

typedef struct {
    uint32_t t_s;   // learner time of the reading
    float dq_Ah;    // charge counter relative to the full charge before it
    float v;        // OCV estimate (V/cell)
    float sv;       // its sigma (V)
} ocv_reading_t;

// Saved to NVM
typedef struct {
    bool valid;
    float s_full;   // factory-curve SOC of the charge voltage, fixed when learning starts
    double corr[OCV_LEARN_BINS];   // learned OCV correction (V)
    double P[OCV_LEARN_BINS][OCV_LEARN_BINS];
    // Full-charge cycles that gave each bin at least one reading's worth of data;
    // decays daily, so old data stops counting as confirmation
    float cycles[OCV_LEARN_BINS];
} ocv_learn_persistent_t;

typedef struct {
    ocv_learn_persistent_t persistent;

    float capacity_ref_Ah;  // Ah from full to OCV_LEARN_V_REF; NAN until the bins there are trusted
    bool dirty;             // persistent data changed and wants saving

    const float *factory;   // the factory OCV table, set on the first tick
    double cap;             // nameplate capacity (Ah), which maps Ah below full to SOC
    float table[101];       // factory + learned correction, for the EKF
    uint32_t t_s;           // seconds since boot (one tick per second)
    double charge_Ah;       // running charge count
    uint32_t quiet_s, low_s, full_s, last_reading_s;
    bool have_anchor;       // a settled full charge seen since boot
    double anchor_Ah;
    uint32_t anchor_s;
    uint16_t n_readings;
    ocv_reading_t readings[OCV_LEARN_MAX_READINGS];
} ocv_learn_t;

// Call once a second with the factory OCV table. Returns true when table has been
// (re)built. charge_V is the per-cell voltage charging stops at; a quiet pack
// within 10mV of it is full.
bool ocv_learn_tick(ocv_learn_t *l, const float factory[101], double capacity_Ah, float charge_V,
                    double charge_Ah, float current_A, float voltage_V);
