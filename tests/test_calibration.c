#include <stddef.h>
#include <setjmp.h>
#include <stdarg.h>
#include <cmocka.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "app/model.h"
#include "app/calibration/common.h"

#include "sys/logging/logging.h"
void logging_printf(log_level_t level, const char *format, ...) {
    (void)level;
    (void)format;
}

#define N_SAMPLES 64.0f
#define MUL_EPSILON 1e-6f

// Values the contactor fields hold before calibration, to check they are left
// alone when calibration fails
#define PRIOR_MUL 0.0123f
#define PRIOR_OFFSET_MV 42.0f

static bms_model_t cal_model;

static int setup(void **state) {
    (void)state;
    memset(&cal_model, 0, sizeof(model));
    cal_model.neg_contactor_mul = PRIOR_MUL;
    cal_model.neg_contactor_offset_mV = PRIOR_OFFSET_MV;
    cal_model.pos_contactor_mul = PRIOR_MUL;
    cal_model.pos_contactor_offset_mV = PRIOR_OFFSET_MV;
    return 0;
}

// Raw accumulator value for a closed contactor reading a few counts per sample
static int32_t small_raw(void) {
    return (int32_t)(5 * N_SAMPLES);
}

// Raw accumulator value far outside the allowed offset range (~65-80V)
static int32_t large_raw(void) {
    return (int32_t)(5000 * N_SAMPLES);
}

static void assert_float_near(float actual, float expected) {
    assert_true(fabsf(actual - expected) < MUL_EPSILON);
}

// ============================================================================
// effective_voltage_mul
// ============================================================================

static void test_effective_mul_falls_back_to_default(void **state) {
    (void)state;
    assert_float_near(effective_voltage_mul(0.0f), ADS1115_DEFAULT_MUL);
    assert_float_near(effective_voltage_mul(0.0123f), 0.0123f);
}

// ============================================================================
// Negative contactor
// ============================================================================

static void test_neg_uses_default_for_uncalibrated_battery_mul(void **state) {
    (void)state;
    // e.g. online calibration with contactors closed, where battery voltage
    // has never been calibrated
    cal_model.battery_voltage_mul = 0.0f;
    cal_model.output_voltage_mul = ADS1115_DEFAULT_MUL * 1.02f;

    assert_true(calibrate_neg_contactor(&cal_model, small_raw(), N_SAMPLES));

    float expected_mul = (ADS1115_DEFAULT_MUL + cal_model.output_voltage_mul) / 2.0f;
    assert_float_near(cal_model.neg_contactor_mul, expected_mul);
    assert_float_near(cal_model.neg_contactor_offset_mV, -(small_raw() * expected_mul * 1000.0f / N_SAMPLES));
}

static void test_neg_offset_out_of_range_leaves_model_unchanged(void **state) {
    (void)state;
    cal_model.battery_voltage_mul = 0.0f;
    cal_model.output_voltage_mul = 0.0f;

    assert_false(calibrate_neg_contactor(&cal_model, large_raw(), N_SAMPLES));

    assert_float_near(cal_model.neg_contactor_mul, PRIOR_MUL);
    assert_float_near(cal_model.neg_contactor_offset_mV, PRIOR_OFFSET_MV);
}

static void test_neg_mul_out_of_range_leaves_model_unchanged(void **state) {
    (void)state;
    cal_model.battery_voltage_mul = 0.05f;
    cal_model.output_voltage_mul = 0.05f;

    assert_false(calibrate_neg_contactor(&cal_model, small_raw(), N_SAMPLES));

    assert_float_near(cal_model.neg_contactor_mul, PRIOR_MUL);
    assert_float_near(cal_model.neg_contactor_offset_mV, PRIOR_OFFSET_MV);
}

// ============================================================================
// Positive contactor (rev1 measures it directly and calibrates its offset)
// ============================================================================

#if IS_BMS_BOARD(REV1)

static void test_pos_uses_default_for_uncalibrated_battery_mul(void **state) {
    (void)state;
    cal_model.battery_voltage_mul = 0.0f;
    cal_model.link_voltage_mul = ADS1115_DEFAULT_MUL * 1.02f;

    assert_true(calibrate_pos_contactor(&cal_model, small_raw(), N_SAMPLES));

    // Previously this was link_voltage_mul / 2, i.e. half scale
    float expected_mul = (ADS1115_DEFAULT_MUL + cal_model.link_voltage_mul) / 2.0f;
    assert_float_near(cal_model.pos_contactor_mul, expected_mul);
    assert_float_near(cal_model.pos_contactor_offset_mV, -(small_raw() * expected_mul * 1000.0f / N_SAMPLES));
}

static void test_pos_uses_default_for_uncalibrated_link_mul(void **state) {
    (void)state;
    // e.g. long calibration where link calibration failed
    cal_model.battery_voltage_mul = ADS1115_DEFAULT_MUL * 0.98f;
    cal_model.link_voltage_mul = 0.0f;

    assert_true(calibrate_pos_contactor(&cal_model, small_raw(), N_SAMPLES));

    float expected_mul = (cal_model.battery_voltage_mul + ADS1115_DEFAULT_MUL) / 2.0f;
    assert_float_near(cal_model.pos_contactor_mul, expected_mul);
}

static void test_pos_offset_out_of_range_leaves_model_unchanged(void **state) {
    (void)state;
    cal_model.battery_voltage_mul = 0.0f;
    cal_model.link_voltage_mul = 0.0f;

    assert_false(calibrate_pos_contactor(&cal_model, large_raw(), N_SAMPLES));

    assert_float_near(cal_model.pos_contactor_mul, PRIOR_MUL);
    assert_float_near(cal_model.pos_contactor_offset_mV, PRIOR_OFFSET_MV);
}

static void test_pos_mul_out_of_range_leaves_model_unchanged(void **state) {
    (void)state;
    cal_model.battery_voltage_mul = 0.05f;
    cal_model.link_voltage_mul = 0.05f;

    assert_false(calibrate_pos_contactor(&cal_model, small_raw(), N_SAMPLES));

    assert_float_near(cal_model.pos_contactor_mul, PRIOR_MUL);
    assert_float_near(cal_model.pos_contactor_offset_mV, PRIOR_OFFSET_MV);
}

#endif

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup(test_effective_mul_falls_back_to_default, setup),
        cmocka_unit_test_setup(test_neg_uses_default_for_uncalibrated_battery_mul, setup),
        cmocka_unit_test_setup(test_neg_offset_out_of_range_leaves_model_unchanged, setup),
        cmocka_unit_test_setup(test_neg_mul_out_of_range_leaves_model_unchanged, setup),
#if IS_BMS_BOARD(REV1)
        cmocka_unit_test_setup(test_pos_uses_default_for_uncalibrated_battery_mul, setup),
        cmocka_unit_test_setup(test_pos_uses_default_for_uncalibrated_link_mul, setup),
        cmocka_unit_test_setup(test_pos_offset_out_of_range_leaves_model_unchanged, setup),
        cmocka_unit_test_setup(test_pos_mul_out_of_range_leaves_model_unchanged, setup),
#endif
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
