#include <stddef.h>
#include <setjmp.h>
#include <stdarg.h>
#include <cmocka.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>

#include "app/isolation.h"
#include "config/limits.h"

#define COUNT_V (4.096f / 32768.0f)

// Raw channel counts for a pack floating around `zero`, with `pos_share` of the
// pack voltage on the + side, through a divider of `ratio`.
static void make_counts(float zero, float battery_V, float ratio, float pos_share,
                        float *pos, float *neg) {
    float span = battery_V / (ratio * COUNT_V);
    *pos = zero + span * pos_share;
    *neg = zero - span * (1.0f - pos_share);
}

static void test_zero_default_and_drift(void **state) {
    (void)state;
    float at_cal = isolation_zero_counts(0.0f, (int32_t)ISOLATION_ZERO_CAL_TEMP_DC);
    assert_float_equal(at_cal, ISOLATION_ZERO_DEFAULT_COUNTS, 0.01f);

    float warmer = isolation_zero_counts(0.0f, (int32_t)ISOLATION_ZERO_CAL_TEMP_DC + 100);
    assert_float_equal(warmer - at_cal, 100.0f * ISOLATION_ZERO_TEMPCO_COUNTS_PER_DC, 0.01f);
}

static void test_zero_calibration_round_trip(void **state) {
    (void)state;
    float ref = isolation_zero_ref_from_counts(15000.0f, 337);
    assert_float_equal(isolation_zero_counts(ref, 337), 15000.0f, 0.01f);
    // A stored calibration replaces the board default
    assert_float_equal(isolation_zero_counts(ref, (int32_t)ISOLATION_ZERO_CAL_TEMP_DC), ref, 0.01f);
}

static void test_floating_pack_is_symmetric_and_healthy(void **state) {
    (void)state;
    float pos, neg;
    make_counts(13317.0f, 383.3f, 501.0f, 0.5f, &pos, &neg);
    isolation_result_t r = isolation_compute(pos, neg, 13317.0f, 383.3f, true);
    assert_float_equal(r.pos_to_chassis_V, 191.65f, 0.05f);
    assert_float_equal(r.neg_to_chassis_V, -191.65f, 0.05f);
    assert_float_equal(r.resistance_ohms, ISOLATION_RESISTANCE_CAP_OHMS, 1.0f);
}

static void test_closed_contactors_give_voltages_but_no_resistance(void **state) {
    (void)state;
    // ck0 reading with the contactors closed to the (faulted) inverter
    isolation_result_t r = isolation_compute(17788.0f, 11620.0f, 13317.0f, 383.3f, false);
    assert_float_equal(r.pos_to_chassis_V, 277.8f, 0.2f);
    assert_float_equal(r.neg_to_chassis_V, -105.5f, 0.2f);
    assert_float_equal(r.pos_to_chassis_V - r.neg_to_chassis_V, 383.3f, 0.01f);
    assert_true(isnan(r.resistance_ohms));
}

static void test_single_sided_leaks(void **state) {
    (void)state;
    const float ratio = 501.0f;
    const float rd = ratio * ISOLATION_DIVIDER_BOTTOM_OHMS;
    const float leak = 1e6f;
    const float rd_par = rd * leak / (rd + leak);
    float pos, neg;

    // 1 Mohm from pack+ to chassis: the + side carries rd||leak against rd
    make_counts(13317.0f, 380.0f, ratio, rd_par / (rd_par + rd), &pos, &neg);
    isolation_result_t r = isolation_compute(pos, neg, 13317.0f, 380.0f, true);
    assert_float_equal(r.resistance_ohms, leak, leak * 0.01f);

    // 1 Mohm from pack- to chassis
    make_counts(13317.0f, 380.0f, ratio, rd / (rd_par + rd), &pos, &neg);
    r = isolation_compute(pos, neg, 13317.0f, 380.0f, true);
    assert_float_equal(r.resistance_ohms, leak, leak * 0.01f);
}

static void test_hard_short_reads_zero(void **state) {
    (void)state;
    // pack+ at chassis: the + channel sits on the zero
    isolation_result_t r = isolation_compute(13317.0f, 13317.0f - 6000.0f, 13317.0f, 380.0f, true);
    assert_float_equal(r.resistance_ohms, 0.0f, 0.001f);
}

static void test_unusable_inputs_give_nan(void **state) {
    (void)state;
    isolation_result_t r = isolation_compute(13320.0f, 13310.0f, 13317.0f, 380.0f, true);
    assert_true(isnan(r.pos_to_chassis_V));
    assert_true(isnan(r.neg_to_chassis_V));
    assert_true(isnan(r.resistance_ohms));

    r = isolation_compute(17788.0f, 11620.0f, 13317.0f, 0.0f, true);
    assert_true(isnan(r.pos_to_chassis_V));
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_zero_default_and_drift),
        cmocka_unit_test(test_zero_calibration_round_trip),
        cmocka_unit_test(test_floating_pack_is_symmetric_and_healthy),
        cmocka_unit_test(test_closed_contactors_give_voltages_but_no_resistance),
        cmocka_unit_test(test_single_sided_leaks),
        cmocka_unit_test(test_hard_short_reads_zero),
        cmocka_unit_test(test_unusable_inputs_give_nan),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
