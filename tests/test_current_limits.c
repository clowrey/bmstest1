#include <stddef.h>
#include <setjmp.h>
#include <stdarg.h>
#include <cmocka.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "sys/time/time.h"
#include "app/battery/current_limits.h"
#include "config/limits.h"

// Mock globals
millis_t stored_millis = 0;
millis64_t stored_millis64 = 0;

// Mock OCV-to-SoC curve for testing
static const float test_nmc_ocv_curve[] = {
    2.50005757, 3.10031943, 3.24625788, 3.33618626, 3.40073441,
    3.44897447, 3.46893947, 3.47395086, 3.47864389, 3.48333693,
    3.48834082, 3.49409536, 3.5013001 , 3.50967053, 3.51931585,
    3.52977034, 3.53957733, 3.54936385, 3.55979467, 3.56845999,
    3.57621643, 3.5856238 , 3.5952045 , 3.60126826, 3.60541126,
    3.60924125, 3.61263967, 3.61573646, 3.61867025, 3.6216477 ,
    3.62429166, 3.62713001, 3.62979388, 3.63270688, 3.6354027 ,
    3.6382091 , 3.64104817, 3.64373467, 3.64666554, 3.6496491 ,
    3.65261197, 3.65568686, 3.6591093 , 3.66244314, 3.666044  ,
    3.6696043 , 3.67341564, 3.67755864, 3.68202541, 3.6866434 ,
    3.69144414, 3.69704364, 3.70266863, 3.70969914, 3.71740244,
    3.72737144, 3.73953482, 3.75054572, 3.75889634, 3.76654077,
    3.77382326, 3.78142929, 3.78922413, 3.79728866, 3.80564429,
    3.8144326 , 3.82301974, 3.83208227, 3.8411448 , 3.85059132,
    3.86039399, 3.86997469, 3.88014603, 3.8899815 , 3.90001143,
    3.91023944, 3.92053556, 3.93082809, 3.94124702, 3.95179866,
    3.96235322, 3.973037  , 3.98377727, 3.99456048, 4.00572681,
    4.01705073, 4.02805948, 4.03954935, 4.05100517, 4.06243052,
    4.07434177, 4.0861864 , 4.09816332, 4.11036443, 4.12256861,
    4.13502932, 4.14774885, 4.16033888, 4.17348003, 4.18652358,
    4.20008564
};

// Required extern function for current_limits.c
float nmc_ocv_to_soc(float ocv) {
    if (ocv <= test_nmc_ocv_curve[0]) return 0.0f;
    if (ocv >= test_nmc_ocv_curve[100]) return 1.0f;
    for (int i = 0; i < 100; i++) {
        if (ocv < test_nmc_ocv_curve[i + 1]) {
            float frac = (ocv - test_nmc_ocv_curve[i]) / (test_nmc_ocv_curve[i + 1] - test_nmc_ocv_curve[i]);
            return (i + frac) / 100.0f;
        }
    }
    return 1.0f;
}

// ============================================================================
// Test Helpers
// ============================================================================

static int setup(void **state) {
    (void) state;
    stored_millis = 1000;
    stored_millis64 = 1000;
    return 0;
}

// ============================================================================
// Cell Voltage Charge Limit Tests
// ============================================================================

static void test_charge_limit_normal_voltage(void **state) {
    (void) state;
    
    // Normal operating voltage range - should allow full charging
    uint16_t limit = calculate_cell_voltage_charge_current_limit(3500, 3800);
    
    // Should return max limit (0xFFFF means no limit from this function)
    assert_int_equal(limit, 0xFFFF);
}

static void test_charge_limit_near_max_voltage(void **state) {
    (void) state;
    
    // Voltage just below soft max - should start derating
    uint16_t limit = calculate_cell_voltage_charge_current_limit(3500, 4100);
    
    // Should be derated but still positive
    assert_true(limit > 0);
    assert_true(limit < 0xFFFF);
}

static void test_charge_limit_at_soft_max(void **state) {
    (void) state;
    
    // At soft max voltage, charging should be stopped
    uint16_t limit = calculate_cell_voltage_charge_current_limit(3500, CELL_VOLTAGE_SOFT_MAX_mV + 1);
    
    assert_int_equal(limit, 0);
}

static void test_charge_limit_at_low_cell_voltage(void **state) {
    (void) state;
    
    // Below soft min - charge limit is restricted to allow recovery
    uint16_t limit = calculate_cell_voltage_charge_current_limit(CELL_VOLTAGE_SOFT_MIN_mV - 100, 3500);
    
    // Should be limited to the overdischarge charge limit
    assert_int_equal(limit, OVERDISCHARGE_CHARGE_CURRENT_LIMIT_dA);
}

// ============================================================================
// Cell Voltage Discharge Limit Tests
// ============================================================================

static void test_discharge_limit_normal_voltage(void **state) {
    (void) state;
    
    // Normal operating voltage - should allow full discharge
    uint16_t limit = calculate_cell_voltage_discharge_current_limit(3700, 3800);
    
    assert_int_equal(limit, 0xFFFF);
}

static void test_discharge_limit_low_voltage_derate(void **state) {
    (void) state;
    
    // Low voltage - should derate discharge
    uint16_t limit = calculate_cell_voltage_discharge_current_limit(3300, 3800);
    
    // Should be derated
    assert_true(limit > 0);
    assert_true(limit < 0xFFFF);
}

static void test_discharge_limit_at_soft_min(void **state) {
    (void) state;
    
    // At soft min voltage, discharge should be stopped
    uint16_t limit = calculate_cell_voltage_discharge_current_limit(CELL_VOLTAGE_SOFT_MIN_mV - 1, 3800);
    
    assert_int_equal(limit, 0);
}

static void test_discharge_limit_at_high_cell_voltage(void **state) {
    (void) state;
    
    // Above soft max - discharge is limited to allow recovery
    uint16_t limit = calculate_cell_voltage_discharge_current_limit(3700, CELL_VOLTAGE_SOFT_MAX_mV + 100);
    
    // Should be limited to the overcharge discharge limit
    assert_int_equal(limit, OVERCHARGE_DISCHARGE_CURRENT_LIMIT_dA);
}

// ============================================================================
// Temperature Charge Limit Tests
// ============================================================================

static void test_temp_charge_limit_normal_temperature(void **state) {
    (void) state;
    
    // Normal temperature range (25C = 250 dC)
    uint16_t limit = calculate_temperature_charge_current_limit(250, 300);
    
    // Should have a reasonable limit
    assert_true(limit > 0);
}

static void test_temp_charge_limit_at_max_temp(void **state) {
    (void) state;
    
    // At max charge temperature limit
    uint16_t limit = calculate_temperature_charge_current_limit(250, MAX_CHARGE_TEMPERATURE_LIMIT_dC);
    
    assert_int_equal(limit, 0);
}

static void test_temp_charge_limit_at_min_temp(void **state) {
    (void) state;
    
    // At min charge temperature limit  
    uint16_t limit = calculate_temperature_charge_current_limit(MIN_CHARGE_TEMPERATURE_LIMIT_dC, 250);
    
    assert_int_equal(limit, 0);
}

static void test_temp_charge_limit_cold_derate(void **state) {
    (void) state;
    
    // Cold battery - should derate
    // 5C = 50 dC, slightly above min limit (0)
    uint16_t limit = calculate_temperature_charge_current_limit(50, 200);
    
    // Should be limited based on proximity to min temp
    uint16_t expected = (50 - MIN_CHARGE_TEMPERATURE_LIMIT_dC) * CHARGE_TEMPERATURE_DERATE_dA_PER_dC;
    assert_int_equal(limit, expected);
}

static void test_temp_charge_limit_hot_derate(void **state) {
    (void) state;
    
    // Hot battery - should derate
    // 45C = 450 dC, approaching max limit (500)
    uint16_t limit = calculate_temperature_charge_current_limit(250, 450);
    
    // Should be limited based on proximity to max temp
    uint16_t expected = (MAX_CHARGE_TEMPERATURE_LIMIT_dC - 450) * CHARGE_TEMPERATURE_DERATE_dA_PER_dC;
    assert_int_equal(limit, expected);
}

// ============================================================================
// Temperature Discharge Limit Tests
// ============================================================================

static void test_temp_discharge_limit_normal_temperature(void **state) {
    (void) state;
    
    // Normal temperature range
    uint16_t limit = calculate_temperature_discharge_current_limit(250, 300);
    
    assert_true(limit > 0);
}

static void test_temp_discharge_limit_at_max_temp(void **state) {
    (void) state;
    
    // At max discharge temperature
    uint16_t limit = calculate_temperature_discharge_current_limit(250, MAX_DISCHARGE_TEMPERATURE_LIMIT_dC);
    
    assert_int_equal(limit, 0);
}

static void test_temp_discharge_limit_at_min_temp(void **state) {
    (void) state;
    
    // At min discharge temperature
    uint16_t limit = calculate_temperature_discharge_current_limit(MIN_DISCHARGE_TEMPERATURE_LIMIT_dC, 250);
    
    assert_int_equal(limit, 0);
}

static void test_temp_discharge_allows_colder_than_charge(void **state) {
    (void) state;
    
    // Discharge allows colder temps than charge
    // -3C = -30 dC (below charge min of 0, but above discharge min of -50)
    uint16_t charge_limit = calculate_temperature_charge_current_limit(-30, 200);
    uint16_t discharge_limit = calculate_temperature_discharge_current_limit(-30, 200);
    
    // Charge should be cut off
    assert_int_equal(charge_limit, 0);
    // Discharge should still be allowed (derated)
    assert_true(discharge_limit > 0);
}

// ============================================================================
// Edge Case Tests
// ============================================================================

static void test_symmetric_voltages(void **state) {
    (void) state;
    
    // When min == max, limits should be consistent
    uint16_t charge_limit = calculate_cell_voltage_charge_current_limit(3700, 3700);
    uint16_t discharge_limit = calculate_cell_voltage_discharge_current_limit(3700, 3700);
    
    // Both should allow current at normal voltage
    assert_int_equal(charge_limit, 0xFFFF);
    assert_int_equal(discharge_limit, 0xFFFF);
}

static void test_extreme_voltage_imbalance(void **state) {
    (void) state;
    
    // Large imbalance: one cell low, one high
    uint16_t charge_limit = calculate_cell_voltage_charge_current_limit(
        CELL_VOLTAGE_SOFT_MIN_mV - 100, 
        CELL_VOLTAGE_SOFT_MAX_mV + 100
    );
    uint16_t discharge_limit = calculate_cell_voltage_discharge_current_limit(
        CELL_VOLTAGE_SOFT_MIN_mV - 100,
        CELL_VOLTAGE_SOFT_MAX_mV + 100
    );
    
    // Both charge and discharge should be severely limited
    assert_int_equal(charge_limit, 0);  // Can't charge - too high
    assert_int_equal(discharge_limit, 0);  // Can't discharge - too low
}

// ============================================================================
// Main
// ============================================================================

int main(void) {
    const struct CMUnitTest tests[] = {
        // Cell voltage charge limits
        cmocka_unit_test_setup(test_charge_limit_normal_voltage, setup),
        cmocka_unit_test_setup(test_charge_limit_near_max_voltage, setup),
        cmocka_unit_test_setup(test_charge_limit_at_soft_max, setup),
        cmocka_unit_test_setup(test_charge_limit_at_low_cell_voltage, setup),
        
        // Cell voltage discharge limits
        cmocka_unit_test_setup(test_discharge_limit_normal_voltage, setup),
        cmocka_unit_test_setup(test_discharge_limit_low_voltage_derate, setup),
        cmocka_unit_test_setup(test_discharge_limit_at_soft_min, setup),
        cmocka_unit_test_setup(test_discharge_limit_at_high_cell_voltage, setup),
        
        // Temperature charge limits
        cmocka_unit_test_setup(test_temp_charge_limit_normal_temperature, setup),
        cmocka_unit_test_setup(test_temp_charge_limit_at_max_temp, setup),
        cmocka_unit_test_setup(test_temp_charge_limit_at_min_temp, setup),
        cmocka_unit_test_setup(test_temp_charge_limit_cold_derate, setup),
        cmocka_unit_test_setup(test_temp_charge_limit_hot_derate, setup),
        
        // Temperature discharge limits
        cmocka_unit_test_setup(test_temp_discharge_limit_normal_temperature, setup),
        cmocka_unit_test_setup(test_temp_discharge_limit_at_max_temp, setup),
        cmocka_unit_test_setup(test_temp_discharge_limit_at_min_temp, setup),
        cmocka_unit_test_setup(test_temp_discharge_allows_colder_than_charge, setup),
        
        // Edge cases
        cmocka_unit_test_setup(test_symmetric_voltages, setup),
        cmocka_unit_test_setup(test_extreme_voltage_imbalance, setup),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
