#include <stddef.h>
#include <setjmp.h>
#include <stdarg.h>
#include <cmocka.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "lib/sampler.h"

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
// Basic Sampler Tests
// ============================================================================

static void test_sampler_initial_state(void **state) {
    (void) state;
    sampler_t s = {0};
    
    assert_int_equal(s.accumulator, 0);
    assert_int_equal(s.sample_count, 0);
    assert_int_equal(s.value, 0);
}

static void test_sampler_single_sample_no_finalize(void **state) {
    (void) state;
    sampler_t s = {0};
    
    // Add one sample, but don't reach max_samples
    sampler_add(&s, 1000, 4, 0);
    
    // Should accumulate but not finalize
    assert_int_equal(s.sample_count, 1);
    assert_int_equal(s.accumulator, 1000);
    assert_int_equal(s.value, 0);  // Not finalized yet
}

static void test_sampler_finalize_on_max_samples(void **state) {
    (void) state;
    sampler_t s = {0};
    
    // Add samples until max_samples is reached
    sampler_add(&s, 100, 4, 0);
    sampler_add(&s, 200, 4, 0);
    sampler_add(&s, 300, 4, 0);
    sampler_add(&s, 400, 4, 0);  // 4th sample triggers finalize
    
    // Should have finalized
    assert_int_equal(s.sample_count, 0);  // Reset after finalize
    assert_int_equal(s.value, 1000);  // 100 + 200 + 300 + 400
    assert_int_equal(s.timestamp, stored_millis);
}

static void test_sampler_tracks_min_max(void **state) {
    (void) state;
    sampler_t s = {0};
    
    sampler_add(&s, 500, 4, 0);
    sampler_add(&s, 100, 4, 0);  // min
    sampler_add(&s, 900, 4, 0);  // max
    sampler_add(&s, 300, 4, 0);
    
    assert_int_equal(s.min_value, 100);
    assert_int_equal(s.max_value, 900);
}

static void test_sampler_with_divide_shift(void **state) {
    (void) state;
    sampler_t s = {0};
    
    // With divide_shift = 2, each sample is divided by 4 (right shift 2)
    sampler_add(&s, 400, 4, 2);  // 400 >> 2 = 100
    sampler_add(&s, 400, 4, 2);  // 100
    sampler_add(&s, 400, 4, 2);  // 100
    sampler_add(&s, 400, 4, 2);  // 100
    
    // Total accumulated: 400
    assert_int_equal(s.value, 400);
}

static void test_sampler_resets_after_finalize(void **state) {
    (void) state;
    sampler_t s = {0};
    
    // First batch
    for (int i = 0; i < 4; i++) {
        sampler_add(&s, 100, 4, 0);
    }
    assert_int_equal(s.value, 400);
    
    // Second batch
    for (int i = 0; i < 4; i++) {
        sampler_add(&s, 200, 4, 0);
    }
    assert_int_equal(s.value, 800);
}

// ============================================================================
// Edge Case Tests
// ============================================================================

static void test_sampler_negative_values(void **state) {
    (void) state;
    sampler_t s = {0};
    
    sampler_add(&s, -100, 4, 0);
    sampler_add(&s, -200, 4, 0);
    sampler_add(&s, 50, 4, 0);
    sampler_add(&s, -50, 4, 0);
    
    assert_int_equal(s.value, -300);
    assert_int_equal(s.min_value, -200);
    assert_int_equal(s.max_value, 50);
}

static void test_sampler_mixed_signs(void **state) {
    (void) state;
    sampler_t s = {0};
    
    sampler_add(&s, 1000, 4, 0);
    sampler_add(&s, -500, 4, 0);
    sampler_add(&s, 500, 4, 0);
    sampler_add(&s, -1000, 4, 0);
    
    assert_int_equal(s.value, 0);  // Sum is zero
    assert_int_equal(s.min_value, -1000);
    assert_int_equal(s.max_value, 1000);
}

static void test_sampler_single_sample_batch(void **state) {
    (void) state;
    sampler_t s = {0};
    
    // max_samples = 1 means immediate finalize
    sampler_add(&s, 42, 1, 0);
    
    assert_int_equal(s.value, 42);
    assert_int_equal(s.sample_count, 0);
}

static void test_sampler_timestamp_updates(void **state) {
    (void) state;
    sampler_t s = {0};
    
    // First batch at t=1000
    for (int i = 0; i < 4; i++) {
        sampler_add(&s, 100, 4, 0);
    }
    assert_int_equal(s.timestamp, 1000);
    
    // Advance time
    advance_time(500);
    
    // Second batch at t=1500
    for (int i = 0; i < 4; i++) {
        sampler_add(&s, 100, 4, 0);
    }
    assert_int_equal(s.timestamp, 1500);
}

static void test_sampler_large_sample_count(void **state) {
    (void) state;
    sampler_t s = {0};
    
    // Large number of samples with divide_shift to prevent overflow
    // Each sample contributes 1000 >> 4 = 62 (truncated)
    for (int i = 0; i < 16; i++) {
        sampler_add(&s, 1000, 16, 4);
    }
    
    // 16 samples of 62 each = 992
    assert_int_equal(s.value, 992);
}

// ============================================================================
// Averaging Test
// ============================================================================

static void test_sampler_effective_averaging(void **state) {
    (void) state;
    sampler_t s = {0};
    
    // To get an average, accumulate with shift, then divide result
    // For 8 samples, use shift=3 (divide by 8), then get average directly
    sampler_add(&s, 800, 8, 3);  // 800 >> 3 = 100
    sampler_add(&s, 400, 8, 3);  // 400 >> 3 = 50
    sampler_add(&s, 200, 8, 3);  // 200 >> 3 = 25
    sampler_add(&s, 600, 8, 3);  // 600 >> 3 = 75
    sampler_add(&s, 800, 8, 3);  // 100
    sampler_add(&s, 400, 8, 3);  // 50
    sampler_add(&s, 200, 8, 3);  // 25
    sampler_add(&s, 600, 8, 3);  // 75
    
    // Sum of shifted values: 500
    // This represents the accumulated "average" (with some truncation error)
    int32_t avg = sampler_get_value(&s, 1);
    assert_int_equal(avg, 500);
}

// ============================================================================
// Main
// ============================================================================

int main(void) {
    const struct CMUnitTest tests[] = {
        // Basic tests
        cmocka_unit_test_setup(test_sampler_initial_state, setup),
        cmocka_unit_test_setup(test_sampler_single_sample_no_finalize, setup),
        cmocka_unit_test_setup(test_sampler_finalize_on_max_samples, setup),
        cmocka_unit_test_setup(test_sampler_tracks_min_max, setup),
        cmocka_unit_test_setup(test_sampler_with_divide_shift, setup),
        cmocka_unit_test_setup(test_sampler_resets_after_finalize, setup),
        
        // Edge cases
        cmocka_unit_test_setup(test_sampler_negative_values, setup),
        cmocka_unit_test_setup(test_sampler_mixed_signs, setup),
        cmocka_unit_test_setup(test_sampler_single_sample_batch, setup),
        cmocka_unit_test_setup(test_sampler_timestamp_updates, setup),
        cmocka_unit_test_setup(test_sampler_large_sample_count, setup),
        
        // Averaging
        cmocka_unit_test_setup(test_sampler_effective_averaging, setup),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
