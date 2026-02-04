#include <stddef.h>
#include <setjmp.h>
#include <stdarg.h>
#include <cmocka.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "sys/events/events.h"
#include "sys/time/time.h"

// Mock globals
millis_t stored_millis = 0;
millis64_t stored_millis64 = 0;

// ============================================================================
// Test Helpers
// ============================================================================

static void advance_time(uint32_t ms) {
    stored_millis += ms;
    stored_millis64 += ms;
}

static void reset_events(void) {
    extern bms_event_slot_t bms_event_slots[ERR_HIGHEST];
    for (int i = 0; i < ERR_HIGHEST; i++) {
        bms_event_slots[i] = (bms_event_slot_t){0};
        bms_event_slots[i].level = LEVEL_WARNING;
        clear_bms_event(i);
    }
}

static int setup(void **state) {
    (void) state;
    stored_millis = 1000;
    stored_millis64 = 1000;
    reset_events();
    return 0;
}

// ============================================================================
// Event Recording Tests
// ============================================================================

static void test_raise_event_sets_level(void **state) {
    (void) state;
    
    raise_bms_event(ERR_CELL_VOLTAGE_HIGH, 3900);
    
    assert_int_equal(get_event_level(ERR_CELL_VOLTAGE_HIGH), LEVEL_WARNING);
}

static void test_raise_event_stores_data(void **state) {
    (void) state;
    extern bms_event_slot_t bms_event_slots[];
    
    raise_bms_event(ERR_CELL_VOLTAGE_HIGH, 0x1234567890ABCDEF);
    
    assert_int_equal(bms_event_slots[ERR_CELL_VOLTAGE_HIGH].data64, 0x1234567890ABCDEF);
}

static void test_raise_event_increments_count(void **state) {
    (void) state;
    
    raise_bms_event(ERR_CELL_VOLTAGE_HIGH, 0);
    assert_int_equal(get_event_count(ERR_CELL_VOLTAGE_HIGH), 1);
    
    // Raising same event again without repeat flag shouldn't increment
    raise_bms_event(ERR_CELL_VOLTAGE_HIGH, 0);
    assert_int_equal(get_event_count(ERR_CELL_VOLTAGE_HIGH), 1);
}

static void test_count_event_increments_each_time(void **state) {
    (void) state;
    
    count_bms_event(ERR_CONTACTOR_POS_STUCK_OPEN, 0);
    count_bms_event(ERR_CONTACTOR_POS_STUCK_OPEN, 0);
    count_bms_event(ERR_CONTACTOR_POS_STUCK_OPEN, 0);
    
    assert_int_equal(get_event_count(ERR_CONTACTOR_POS_STUCK_OPEN), 3);
}

static void test_clear_event_resets_level(void **state) {
    (void) state;
    
    raise_bms_event(ERR_CELL_VOLTAGE_HIGH, 0);
    assert_int_equal(get_event_level(ERR_CELL_VOLTAGE_HIGH), LEVEL_WARNING);
    
    clear_bms_event(ERR_CELL_VOLTAGE_HIGH);
    assert_int_equal(get_event_level(ERR_CELL_VOLTAGE_HIGH), LEVEL_NONE);
}

static void test_clear_fatal_event_not_allowed(void **state) {
    (void) state;
    
    // Raise event that will escalate to fatal immediately
    raise_bms_event(ERR_RESTARTING, 0);
    assert_int_equal(get_event_level(ERR_RESTARTING), LEVEL_FATAL);
    
    // Try to clear - should not work
    clear_bms_event(ERR_RESTARTING);
    assert_int_equal(get_event_level(ERR_RESTARTING), LEVEL_FATAL);
}

// ============================================================================
// Escalation Tests (Warning -> Fatal by count)
// ============================================================================

static void test_warning_escalates_after_count_threshold(void **state) {
    (void) state;
    
    // ERR_CONTACTOR_POS_STUCK_OPEN escalates after 20 counts
    for (int i = 0; i < 19; i++) {
        count_bms_event(ERR_CONTACTOR_POS_STUCK_OPEN, 0);
        assert_int_equal(get_event_level(ERR_CONTACTOR_POS_STUCK_OPEN), LEVEL_WARNING);
    }
    
    count_bms_event(ERR_CONTACTOR_POS_STUCK_OPEN, 0);
    assert_int_equal(get_event_level(ERR_CONTACTOR_POS_STUCK_OPEN), LEVEL_FATAL);
}

static void test_warning_no_escalation_with_zero_threshold(void **state) {
    (void) state;
    
    // ERR_CELL_VOLTAGE_HIGH has escalate_after = 0 (never escalates)
    for (int i = 0; i < 100; i++) {
        count_bms_event(ERR_CELL_VOLTAGE_HIGH, 0);
    }
    
    // Should still be warning, not fatal
    assert_int_equal(get_event_level(ERR_CELL_VOLTAGE_HIGH), LEVEL_WARNING);
}

// ============================================================================
// Escalation Tests (Critical -> Fatal by time)
// ============================================================================

static void test_critical_escalates_after_time_threshold(void **state) {
    (void) state;
    
    // ERR_BATTERY_VOLTAGE_VERY_HIGH escalates after 1000ms
    events_tick();  // Initialize last_tick_at
    
    raise_bms_event(ERR_BATTERY_VOLTAGE_VERY_HIGH, 0);
    assert_int_equal(get_event_level(ERR_BATTERY_VOLTAGE_VERY_HIGH), LEVEL_CRITICAL);
    
    // Tick at 500ms - not yet escalated
    advance_time(500);
    events_tick();
    assert_int_equal(get_event_level(ERR_BATTERY_VOLTAGE_VERY_HIGH), LEVEL_CRITICAL);
    
    // Tick at 600ms more (total 1100ms) - should escalate
    advance_time(600);
    events_tick();
    assert_int_equal(get_event_level(ERR_BATTERY_VOLTAGE_VERY_HIGH), LEVEL_FATAL);
}

static void test_critical_immediate_escalation_with_zero_leeway(void **state) {
    (void) state;
    
    // ERR_CONTACTOR_POS_STUCK_CLOSED has escalate_after = 0 (immediate)
    raise_bms_event(ERR_CONTACTOR_POS_STUCK_CLOSED, 0);
    
    events_tick();
    
    assert_int_equal(get_event_level(ERR_CONTACTOR_POS_STUCK_CLOSED), LEVEL_FATAL);
}

static void test_critical_accumulator_decays_when_cleared(void **state) {
    (void) state;
    extern bms_event_slot_t bms_event_slots[];
    
    events_tick();
    
    // Raise critical event
    raise_bms_event(ERR_BATTERY_VOLTAGE_VERY_HIGH, 0);
    
    // Tick to accumulate some time
    advance_time(500);
    events_tick();
    uint16_t accumulated = bms_event_slots[ERR_BATTERY_VOLTAGE_VERY_HIGH].accumulator;
    assert_true(accumulated > 0);
    
    // Clear the event
    clear_bms_event(ERR_BATTERY_VOLTAGE_VERY_HIGH);
    
    // Tick to allow decay
    advance_time(200);
    events_tick();
    
    // Accumulator should have decayed
    assert_true(bms_event_slots[ERR_BATTERY_VOLTAGE_VERY_HIGH].accumulator < accumulated);
}

// ============================================================================
// Highest Level Tracking Tests
// ============================================================================

static void test_highest_level_tracks_max(void **state) {
    (void) state;
    
    assert_int_equal(get_highest_event_level(), LEVEL_NONE);
    
    raise_bms_event(ERR_CELL_VOLTAGE_HIGH, 0);  // WARNING
    assert_int_equal(get_highest_event_level(), LEVEL_WARNING);
    
    raise_bms_event(ERR_BATTERY_VOLTAGE_VERY_HIGH, 0);  // CRITICAL
    assert_int_equal(get_highest_event_level(), LEVEL_CRITICAL);
}

static void test_highest_level_updates_on_clear(void **state) {
    (void) state;
    
    raise_bms_event(ERR_CELL_VOLTAGE_HIGH, 0);  // WARNING
    raise_bms_event(ERR_BATTERY_VOLTAGE_VERY_HIGH, 0);  // CRITICAL
    assert_int_equal(get_highest_event_level(), LEVEL_CRITICAL);
    
    clear_bms_event(ERR_BATTERY_VOLTAGE_VERY_HIGH);
    assert_int_equal(get_highest_event_level(), LEVEL_WARNING);
    
    clear_bms_event(ERR_CELL_VOLTAGE_HIGH);
    assert_int_equal(get_highest_event_level(), LEVEL_NONE);
}

// ============================================================================
// Edge Cases
// ============================================================================

static void test_invalid_event_type_ignored(void **state) {
    (void) state;
    
    // Should not crash
    raise_bms_event(ERR_HIGHEST, 0);
    raise_bms_event(ERR_HIGHEST + 100, 0);
    clear_bms_event(ERR_HIGHEST);
    
    assert_int_equal(get_event_count(ERR_HIGHEST), 0);
}

static void test_event_timestamp_recorded(void **state) {
    (void) state;
    extern bms_event_slot_t bms_event_slots[];
    
    advance_time(500);
    raise_bms_event(ERR_CELL_VOLTAGE_HIGH, 0);
    
    assert_int_equal(bms_event_slots[ERR_CELL_VOLTAGE_HIGH].timestamp, stored_millis);
}

// ============================================================================
// Main
// ============================================================================

int main(void) {
    const struct CMUnitTest tests[] = {
        // Event recording
        cmocka_unit_test_setup(test_raise_event_sets_level, setup),
        cmocka_unit_test_setup(test_raise_event_stores_data, setup),
        cmocka_unit_test_setup(test_raise_event_increments_count, setup),
        cmocka_unit_test_setup(test_count_event_increments_each_time, setup),
        cmocka_unit_test_setup(test_clear_event_resets_level, setup),
        cmocka_unit_test_setup(test_clear_fatal_event_not_allowed, setup),
        
        // Warning escalation by count
        cmocka_unit_test_setup(test_warning_escalates_after_count_threshold, setup),
        cmocka_unit_test_setup(test_warning_no_escalation_with_zero_threshold, setup),
        
        // Critical escalation by time
        cmocka_unit_test_setup(test_critical_escalates_after_time_threshold, setup),
        cmocka_unit_test_setup(test_critical_immediate_escalation_with_zero_leeway, setup),
        cmocka_unit_test_setup(test_critical_accumulator_decays_when_cleared, setup),
        
        // Highest level tracking
        cmocka_unit_test_setup(test_highest_level_tracks_max, setup),
        cmocka_unit_test_setup(test_highest_level_updates_on_clear, setup),
        
        // Edge cases
        cmocka_unit_test_setup(test_invalid_event_type_ignored, setup),
        cmocka_unit_test_setup(test_event_timestamp_recorded, setup),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
