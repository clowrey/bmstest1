#include <stddef.h>
#include <setjmp.h>
#include <stdarg.h>
#include <cmocka.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "app/battery/pack_types.h"
#include "config/limits.h"
#include "drivers/bmb3y/bmb3y.h"
#include "drivers/bmb3y/crc.h"
#include "sys/logging/logging.h"
#include "sys/time/time.h"

void logging_printf(log_level_t level, const char *format, ...) {
    (void)level;
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
}

millis_t stored_millis = 0;
millis64_t stored_millis64 = 0;

int bmb3y_count_live_channels(void) { return -1; }
int bmb3y_count_cell_modules(void) { return -1; }

static const uint16_t VECTORS[][2] = {
    // raw, module ID
    { 0xF804, 0x004 },
    { 0x8805, 0x005 },
    { 0xA006, 0x006 },
    { 0x201A, 0x01A },
    { 0x8023, 0x023 },
    { 0x7860, 0x060 },
};

static bool word_valid(uint16_t raw) {
    return (raw >> BMB3Y_MODULE_ID_CHECK_SHIFT) == module_id_check_bits(raw & BMB3Y_MODULE_ID_MASK);
}

static void test_check_bits(void **state) {
    (void)state;
    for(size_t i=0; i<sizeof(VECTORS)/sizeof(VECTORS[0]); i++) {
        uint16_t raw = VECTORS[i][0];
        assert_int_equal(raw & BMB3Y_MODULE_ID_MASK, VECTORS[i][1]);
        assert_true(word_valid(raw));
        // Any one-bit error fails the check
        for(int bit=0; bit<16; bit++) {
            assert_false(word_valid(raw ^ (uint16_t)(1 << bit)));
        }
    }
}

static pack_context_t context(int num_cells, uint8_t chemistry, int present) {
    return (pack_context_t){
        .num_cells = num_cells, .chemistry = chemistry, .present = present,
        .live_channels = -1, .cell_modules = -1, .mean_cell_mV = -1,
    };
}

static void classify(const uint16_t *ids, int count, pack_context_t ctx, pack_classification_t *c) {
    pack_types_classify_ids(ids, count, &ctx, c);
}

static void test_sample_readout(void **state) {
    (void)state;
    const uint16_t ids[] = { 0x004, 0x005, 0x005, 0x005, 0x006 };
    pack_classification_t c;
    classify(ids, 5, context(96, NMC, 5), &c);
    assert_int_equal(c.result, PACK_RESULT_CLASSIFIED);
    assert_int_equal(c.id_count, 3);
    assert_int_equal(c.candidates[0]->series, 96);
    assert_int_equal(c.candidates[0]->parallel, 46);
    assert_int_equal(c.candidates[0]->range, PACK_RANGE_LR);
    assert_int_equal(c.candidates[0]->temp_sensors, 16);
}

static void test_same_id_on_every_bmb(void **state) {
    (void)state;
    const uint16_t ids[] = { 0x004, 0x004, 0x004, 0x004, 0x004, 0x004, 0x004, 0x004 };
    pack_classification_t c;
    classify(ids, 8, context(96, NMC, 8), &c);
    assert_int_equal(c.result, PACK_RESULT_CLASSIFIED);
    assert_int_equal(c.id_count, 1);
    assert_int_equal(c.candidates[0]->temp_sensors, 16);
}

static void test_ambiguous_sensor_variants(void **state) {
    (void)state;
    const uint16_t ids[] = { 0x005, 0x006 };
    pack_classification_t c;
    classify(ids, 2, context(96, NMC, 2), &c);
    assert_int_equal(c.result, PACK_RESULT_AMBIGUOUS);
    assert_int_equal(c.candidate_count, 2);
    for(int i=0; i<2; i++) {
        assert_int_equal(c.candidates[i]->series, 96);
        assert_int_equal(c.candidates[i]->parallel, 46);
    }
    assert_int_not_equal(c.candidates[0]->temp_sensors, c.candidates[1]->temp_sensors);
}

static void test_shared_id_split_by_profile(void **state) {
    (void)state;
    // 0x03D is listed for both 96s31p SR NMC and 108s1p SR LFP
    const uint16_t ids[] = { 0x03D, 0x03D, 0x03D, 0x03D, 0x03D, 0x03D, 0x03D, 0x03D };
    pack_classification_t c;

    classify(ids, 8, context(108, LFP, 8), &c);
    assert_int_equal(c.result, PACK_RESULT_CLASSIFIED);
    assert_int_equal(c.candidates[0]->series, 108);
    assert_int_equal(c.candidates[0]->chemistry, LFP);
    assert_int_equal(c.candidates[0]->devices, 8);

    classify(ids, 8, context(96, NMC, 8), &c);
    assert_int_equal(c.result, PACK_RESULT_AMBIGUOUS);
    assert_int_equal(c.candidate_count, 2);
    assert_int_equal(c.candidates[0]->parallel, 31);
    assert_int_equal(c.candidates[1]->parallel, 31);
}

static void test_device_count_narrows(void **state) {
    (void)state;
    // 108s1p lists 8 devices, so a single BMB answering doesn't fit it
    const uint16_t ids[] = { 0x03D };
    pack_classification_t c;
    classify(ids, 1, context(108, LFP, 1), &c);
    assert_int_equal(c.result, PACK_RESULT_CONFLICT);
}

static void test_conflict(void **state) {
    (void)state;
    pack_classification_t c;

    // LFP module IDs on an NMC profile
    const uint16_t lfp[] = { 0x023 };
    classify(lfp, 1, context(96, NMC, 8), &c);
    assert_int_equal(c.result, PACK_RESULT_CONFLICT);

    // IDs from two different topologies
    const uint16_t mixed[] = { 0x004, 0x009 };
    classify(mixed, 2, context(96, NMC, 2), &c);
    assert_int_equal(c.result, PACK_RESULT_CONFLICT);
}

static void test_unknown(void **state) {
    (void)state;
    const uint16_t ids[] = { 0x123, 0x004 };
    pack_classification_t c;
    classify(ids, 2, context(96, NMC, 2), &c);
    assert_int_equal(c.result, PACK_RESULT_UNKNOWN);
    assert_int_equal(c.unknown_count, 1);
    assert_int_equal(c.unknown_ids[0], 0x123);
}

static void test_no_ids(void **state) {
    (void)state;
    const pack_type_t *out[PACK_TYPE_MAX_CANDIDATES];
    assert_int_equal(pack_types_match(NULL, 0, out, PACK_TYPE_MAX_CANDIDATES), 0);

    pack_classification_t c;
    classify(NULL, 0, context(96, NMC, 0), &c);
    assert_int_equal(c.result, PACK_RESULT_NO_ID);
}

static void test_cross_checks(void **state) {
    (void)state;
    const uint16_t ids[] = { 0x023, 0x023, 0x023, 0x023, 0x023, 0x023, 0x023, 0x023 };
    pack_context_t ctx = context(108, LFP, 8);
    pack_classification_t c;

    ctx.live_channels = 108;
    ctx.cell_modules = 8;
    ctx.mean_cell_mV = 3300;
    classify(ids, 8, ctx, &c);
    assert_int_equal(c.result, PACK_RESULT_CLASSIFIED);
    assert_true(c.cells_ok);
    assert_true(c.devices_ok);
    assert_false(c.voltage_suggests_nmc);

    ctx.live_channels = 96;
    ctx.cell_modules = 7;
    ctx.mean_cell_mV = 3900;
    classify(ids, 8, ctx, &c);
    assert_false(c.cells_ok);
    assert_false(c.devices_ok);
    assert_true(c.voltage_suggests_nmc);
}

static void test_table_rules(void **state) {
    (void)state;
    int rows = 0;
    for(const pack_type_t *t; (t = pack_type_at(rows)) != NULL; rows++) {
        // Chemistry is inferred from the parallel count
        assert_int_equal(t->chemistry == LFP, t->parallel == 1);
        assert_int_equal(pack_type_index(t), rows);
        assert_int_equal(t->module_id_count == 0, t->module_ids == NULL);
        for(int i=0; i<t->module_id_count; i++) {
            assert_true(t->module_ids[i] < 0x800);
            assert_true(pack_types_id_known(t->module_ids[i]));
        }
    }
    assert_int_equal(rows, 34);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_check_bits),
        cmocka_unit_test(test_sample_readout),
        cmocka_unit_test(test_same_id_on_every_bmb),
        cmocka_unit_test(test_ambiguous_sensor_variants),
        cmocka_unit_test(test_shared_id_split_by_profile),
        cmocka_unit_test(test_device_count_narrows),
        cmocka_unit_test(test_conflict),
        cmocka_unit_test(test_unknown),
        cmocka_unit_test(test_no_ids),
        cmocka_unit_test(test_cross_checks),
        cmocka_unit_test(test_table_rules),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
