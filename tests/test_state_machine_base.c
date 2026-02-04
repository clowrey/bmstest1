#include <stddef.h>
#include <setjmp.h>
#include <stdarg.h>
#include <cmocka.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "app/state_machines/base.h"
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

static int setup(void **state) {
    (void) state;
    stored_millis = 1000;
    stored_millis64 = 1000;
    return 0;
}

// ============================================================================
// State Machine Base Tests
// ============================================================================

static void test_sm_init_sets_state_zero(void **state) {
    (void) state;
    sm_t sm = {.state = 99, .last_transition_time = 12345};
    
    sm_init(&sm, "test");
    
    assert_int_equal(sm.state, 0);
    assert_string_equal(sm.name, "test");
}

static void test_state_transition_updates_time(void **state) {
    (void) state;
    sm_t sm = {0};
    sm_init(&sm, "test");
    
    advance_time(500);
    state_transition(&sm, 5);
    
    assert_int_equal(sm.state, 5);
    assert_int_equal(sm.last_transition_time, stored_millis64);
}

static void test_state_timeout_false_before_timeout(void **state) {
    (void) state;
    sm_t sm = {0};
    sm_init(&sm, "test");
    
    advance_time(500);
    
    assert_false(state_timeout(&sm, 1000));
}

static void test_state_timeout_true_after_timeout(void **state) {
    (void) state;
    sm_t sm = {0};
    sm_init(&sm, "test");
    
    advance_time(1001);
    
    assert_true(state_timeout(&sm, 1000));
}

static void test_state_timeout_true_at_exact_timeout(void **state) {
    (void) state;
    sm_t sm = {0};
    sm_init(&sm, "test");
    
    advance_time(1000);
    
    assert_true(state_timeout(&sm, 1000));
}

static void test_state_time_returns_elapsed(void **state) {
    (void) state;
    sm_t sm = {0};
    sm_init(&sm, "test");
    
    advance_time(750);
    
    assert_int_equal(state_time(&sm), 750);
}

static void test_state_reset_goes_to_zero(void **state) {
    (void) state;
    sm_t sm = {0};
    sm_init(&sm, "test");
    
    state_transition(&sm, 10);
    advance_time(500);
    state_reset(&sm);
    
    assert_int_equal(sm.state, 0);
}

static void test_multiple_transitions_track_time_correctly(void **state) {
    (void) state;
    sm_t sm = {0};
    sm_init(&sm, "test");
    
    advance_time(100);
    state_transition(&sm, 1);
    millis64_t t1 = sm.last_transition_time;
    
    advance_time(200);
    state_transition(&sm, 2);
    millis64_t t2 = sm.last_transition_time;
    
    assert_int_equal(t2 - t1, 200);
    assert_int_equal(sm.state, 2);
}

// ============================================================================
// Main
// ============================================================================

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup(test_sm_init_sets_state_zero, setup),
        cmocka_unit_test_setup(test_state_transition_updates_time, setup),
        cmocka_unit_test_setup(test_state_timeout_false_before_timeout, setup),
        cmocka_unit_test_setup(test_state_timeout_true_after_timeout, setup),
        cmocka_unit_test_setup(test_state_timeout_true_at_exact_timeout, setup),
        cmocka_unit_test_setup(test_state_time_returns_elapsed, setup),
        cmocka_unit_test_setup(test_state_reset_goes_to_zero, setup),
        cmocka_unit_test_setup(test_multiple_transitions_track_time_correctly, setup),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
