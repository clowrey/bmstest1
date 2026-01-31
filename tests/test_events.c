#include <stddef.h>
#include <setjmp.h>
#include <stdarg.h>
#include <cmocka.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "app/model.h"
#include "sys/events/events.h"
#include "app/battery/safety_checks.h"
#include "app/hardware_checks.h"
#include "sys/time/time.h"
#include "config/limits.h"

millis_t stored_millis = 0;
millis64_t stored_millis64 = 0;

static int setup(void **state) {
    (void) state;
    stored_millis = 1000;
    stored_millis64 = 1000;
    extern bms_event_slot_t bms_event_slots[ERR_HIGHEST];
    for(int i = 0; i < ERR_HIGHEST; i++) {
        bms_event_slots[i] = (bms_event_slot_t){0};
        // Strange way to reset events
        bms_event_slots[i].level = LEVEL_WARNING;
        clear_bms_event(i);
    }
    return 0;
}

static void setup_model(bms_model_t *model) {
    // Basic operating state

    model->system_sm.state = SYSTEM_STATE_OPERATING;
    model->battery_voltage_millis = stored_millis;
    model->cell_voltage_millis = stored_millis;
    model->temperature_millis = stored_millis;

    model->cell_voltage_min_mV = 3500;
    model->cell_voltage_max_mV = 3500;
    for(int i = 0; i < 8; i++) {
        model->module_temperatures_dC[i] = 250; // 25.0C
    }
    model->contactor_sm.enable_current = true;
}

static void test_manual_trigger(void **state) {
    (void) state;
    // Test that every event can at least be manually raised
    for (int i = 0; i < ERR_HIGHEST; i++) {
        raise_bms_event(i, 0);
        assert_int_not_equal(get_event_level(i), LEVEL_NONE);
        
        if (get_event_level(i) != LEVEL_FATAL) {
            clear_bms_event(i);
            assert_int_equal(get_event_level(i), LEVEL_NONE);
        }
    }
}

static void test_safety_voltage_events(void **state) {
    (void) state;
    bms_model_t model = {0};
    model.system_sm.state = SYSTEM_STATE_OPERATING;
    model.battery_voltage_millis = stored_millis;
    model.cell_voltage_millis = stored_millis;
    model.temperature_millis = stored_millis;

    // Normal voltages
    model.cell_voltage_min_mV = 3300;
    model.cell_voltage_max_mV = 3300;
    model.cell_voltage_total_mV = model.cell_voltage_max_mV * NUM_CELLS;
    model.battery_voltage_mV = model.cell_voltage_max_mV * NUM_CELLS;
    
    confirm_battery_safety(&model);
    print_bms_events();
    assert_int_equal(get_highest_event_level(), LEVEL_NONE);

    // Battery Voltage High
    model.battery_voltage_mV = BATTERY_VOLTAGE_SOFT_MAX_mV + 1;
    confirm_battery_safety(&model);
    assert_int_equal(get_event_level(ERR_BATTERY_VOLTAGE_HIGH), LEVEL_WARNING);

    // Battery Voltage Very High
    model.battery_voltage_mV = BATTERY_VOLTAGE_HARD_MAX_mV + 1;
    confirm_battery_safety(&model);
    assert_int_equal(get_event_level(ERR_BATTERY_VOLTAGE_VERY_HIGH), LEVEL_CRITICAL);

    // Cell Voltage Low
    model.cell_voltage_min_mV = CELL_VOLTAGE_SOFT_MIN_mV - 1;
    confirm_battery_safety(&model);
    assert_int_equal(get_event_level(ERR_CELL_VOLTAGE_LOW), LEVEL_WARNING);

    // Cell Voltage Very Low
    model.cell_voltage_min_mV = CELL_VOLTAGE_HARD_MIN_mV - 1;
    confirm_battery_safety(&model);
    assert_int_equal(get_event_level(ERR_CELL_VOLTAGE_VERY_LOW), LEVEL_CRITICAL);
}

static void test_safety_temp_events(void **state) {
    (void) state;
    bms_model_t model = {0};
    model.system_sm.state = SYSTEM_STATE_OPERATING;
    model.temperature_millis = stored_millis;

    // High temp
    model.temperature_max_dC = TEMPERATURE_SOFT_MAX_dC + 1;
    confirm_battery_safety(&model);
    assert_int_equal(get_event_level(ERR_BATTERY_TEMPERATURE_HIGH), LEVEL_WARNING);

    // Very high temp
    model.temperature_max_dC = TEMPERATURE_HARD_MAX_dC + 1;
    confirm_battery_safety(&model);
    assert_int_equal(get_event_level(ERR_BATTERY_TEMPERATURE_VERY_HIGH), LEVEL_CRITICAL);
}

static void test_hardware_voltage_events(void **state) {
    (void) state;
    bms_model_t model = {0};
    model.supply_voltage_3V3_millis = stored_millis;
    model.supply_voltage_5V_millis = stored_millis;
    model.supply_voltage_12V_millis = stored_millis;
    model.supply_voltage_contactor_millis = stored_millis;

    // 3.3V Low
    model.supply_voltage_3V3_mV = SUPPLY_VOLTAGE_3V3_MIN_MV - 1;
    confirm_hardware_integrity(&model);
    assert_int_equal(get_event_level(ERR_SUPPLY_VOLTAGE_3V3_LOW), LEVEL_WARNING);

    // Contactor Voltage Very Low
    model.supply_voltage_contactor_mV = SUPPLY_VOLTAGE_CONTACTOR_HARD_MIN_MV - 1;
    confirm_hardware_integrity(&model);
    assert_int_equal(get_event_level(ERR_SUPPLY_VOLTAGE_CONTACTOR_VERY_LOW), LEVEL_CRITICAL);
}

static void test_stale_data_events(void **state) {
    (void) state;
    bms_model_t model = {0};
    model.system_sm.state = SYSTEM_STATE_OPERATING;
    
    // Set all timestamps to old
    model.battery_voltage_millis = stored_millis - BATTERY_VOLTAGE_STALE_THRESHOLD_MS - 1;
    model.cell_voltage_millis = stored_millis - CELL_VOLTAGE_STALE_THRESHOLD_MS - 1;
    model.temperature_millis = stored_millis - TEMPERATURE_STALE_THRESHOLD_MS(&model) - 1;
    
    confirm_battery_safety(&model);
    assert_int_equal(get_event_level(ERR_BATTERY_VOLTAGE_STALE), LEVEL_CRITICAL);
    assert_int_equal(get_event_level(ERR_CELL_VOLTAGES_STALE), LEVEL_CRITICAL);
    assert_int_equal(get_event_level(ERR_BATTERY_TEMPERATURE_STALE), LEVEL_CRITICAL);
}

static void test_event_escalation(void **state) {
    (void) state;
    // Test escalation of WARNING to FATAL after enough counts
    // ERR_CONTACTOR_POS_STUCK_OPEN escalates after 20 counts
    for (int i = 0; i < 19; i++) {
        count_bms_event(ERR_CONTACTOR_POS_STUCK_OPEN, 0);
        assert_int_equal(get_event_level(ERR_CONTACTOR_POS_STUCK_OPEN), LEVEL_WARNING);
    }
    count_bms_event(ERR_CONTACTOR_POS_STUCK_OPEN, 0);
    assert_int_equal(get_event_level(ERR_CONTACTOR_POS_STUCK_OPEN), LEVEL_FATAL);

    // Test escalation of CRITICAL to FATAL after time
    // ERR_BATTERY_VOLTAGE_VERY_HIGH escalates after 1000ms

    // Get timestamps up to date
    events_tick(); 
    // Check event isn't set yet
    assert_int_equal(get_event_level(ERR_BATTERY_VOLTAGE_VERY_HIGH), LEVEL_NONE);
    raise_bms_event(ERR_BATTERY_VOLTAGE_VERY_HIGH, 0);
    assert_int_equal(get_event_level(ERR_BATTERY_VOLTAGE_VERY_HIGH), LEVEL_CRITICAL);
    
    // tick events
    events_tick(); // first tick sets last_tick_at
    
    stored_millis += 500;
    events_tick();
    assert_int_equal(get_event_level(ERR_BATTERY_VOLTAGE_VERY_HIGH), LEVEL_CRITICAL);
    
    stored_millis += 600;
    events_tick();
    assert_int_equal(get_event_level(ERR_BATTERY_VOLTAGE_VERY_HIGH), LEVEL_FATAL);
}

static void test_overcurrent_accumulation(void **state) {
    (void) state;
    bms_model_t model = {0};
    setup_model(&model);

    // Normal current (within limits + margin)
    // 50A limit + 0.5A margin = 50.5A = 50500mA
    model.current_mA = 50000; 
    model_tick(&model);
    assert_int_equal(model.excess_charge_buffer_dC, 0);
    confirm_battery_safety(&model);
    assert_int_equal(get_event_level(ERR_OVERCURRENT_CHARGING), LEVEL_NONE);

    // Excess charge current: 60A = 60000mA
    model.current_mA = 60000;
    model_tick(&model);
    // excess_dA = (60000/100) - 500 - 10 = 600 - 510 = 90 dA
    assert_int_equal(model.excess_charge_buffer_dC, 89);
    
    // Run it enough times to exceed OVERCURRENT_BUFFER_LIMIT_dC (1000)
    // 11 more ticks. Total 12 ticks.
    // 12 * 90 dA = 1080 dC > 1000 dC limit
    for(int i = 0; i < 11; i++) {
        model.current_millis = stored_millis;
        model_tick(&model);
    }
    assert_true(model.excess_charge_buffer_dC > OVERCURRENT_BUFFER_LIMIT_dC);
    confirm_battery_safety(&model);
    assert_int_equal(get_event_level(ERR_OVERCURRENT_CHARGING), LEVEL_CRITICAL);

    // Now test discharge
    model.excess_charge_buffer_dC = 0; // reset
    clear_bms_event(ERR_OVERCURRENT_CHARGING);
    
    // 60A discharge = -60000mA
    model.current_mA = -60000;
    model_tick(&model);
    // excess_dA = (-(-60000)/100) - 500 - 10 = 600 - 510 = 90 dA
    assert_int_equal(model.excess_discharge_buffer_dC, 89);

    for(int i = 0; i < 11; i++) {
        model.current_millis = stored_millis;
        model_tick(&model);
    }
    assert_true(model.excess_discharge_buffer_dC > OVERCURRENT_BUFFER_LIMIT_dC);
    confirm_battery_safety(&model);
    assert_int_equal(get_event_level(ERR_OVERCURRENT_DISCHARGING), LEVEL_CRITICAL);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup(test_manual_trigger, setup),
        cmocka_unit_test_setup(test_safety_voltage_events, setup),
        cmocka_unit_test_setup(test_safety_temp_events, setup),
        cmocka_unit_test_setup(test_hardware_voltage_events, setup),
        cmocka_unit_test_setup(test_stale_data_events, setup),
        cmocka_unit_test_setup(test_event_escalation, setup),
        cmocka_unit_test_setup(test_overcurrent_accumulation, setup),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
