#include "common.h"
#include "sys/logging/logging.h"
#include <math.h>

bool calibrate_battery_voltage(bms_model_t *model, int32_t raw_val, float sample_count) {
    float mul = (float)model->cell_voltage_total_mV * 0.001f * sample_count / (float)raw_val;

    if(mul > MIN_CALIBRATION_MUL && mul < MAX_CALIBRATION_MUL) {
        info_printf("Battery voltage calibration complete: raw=%ld mul=%f cvt=%ld\n", raw_val, mul, model->cell_voltage_total_mV);
        model->battery_voltage_mul = mul;
        return true;
    } else {
        error_printf("Battery voltage calibration out of range: %f\n", mul);
        return false;
    }
}

bool calibrate_output_voltage(bms_model_t *model, int32_t raw_val, float sample_count) {
    float mul = (float)model->cell_voltage_total_mV * 0.001f * sample_count / (float)raw_val;

    if(mul > MIN_CALIBRATION_MUL && mul < MAX_CALIBRATION_MUL) {
        info_printf("Output voltage calibration complete: raw=%ld mul=%f\n", raw_val, mul);
        model->output_voltage_mul = mul;
        return true;
    } else {
        error_printf("Output voltage calibration out of range: %f\n", mul);
        return false;
    }
}

bool calibrate_neg_contactor(bms_model_t *model, int32_t raw_val, float sample_count) {
    // lazily split the difference for now, not quite right but it'll do
    model->neg_contactor_mul = (model->battery_voltage_mul + model->output_voltage_mul) / 2.0f;

    // calculate the offset
    float neg_mV = (float)raw_val * model->neg_contactor_mul * 1000.0f / sample_count;

    debug_printf("Raw negcal=%ld neg_mV=%f, neg_contactor_mul=%f\n", raw_val, neg_mV, model->neg_contactor_mul);

    if(neg_mV > MIN_NEG_CONTACTOR_OFFSET_MV && neg_mV < MAX_NEG_CONTACTOR_OFFSET_MV) {
        info_printf("Neg contactor calibration complete: raw=%ld neg_mV=%f\n", raw_val, neg_mV);
        model->neg_contactor_offset_mV = -neg_mV;
        return true;
    } else {
        error_printf("Neg contactor calibration out of range: %f mV\n", neg_mV);
        return false;
    }
}

bool calibrate_pos_contactor(bms_model_t *model, int32_t raw_val, float sample_count) {
    float mul = (float)model->cell_voltage_total_mV * 0.001f * sample_count / (float)raw_val;

    if(mul > MIN_CALIBRATION_MUL && mul < MAX_CALIBRATION_MUL) {
        info_printf("Pos contactor calibration complete: raw=%ld mul=%f\n", raw_val, mul);
        model->pos_contactor_mul = mul;
        return true;
    } else {
        error_printf("Pos contactor calibration out of range: %f\n", mul);
        return false;
    }
}

bool calibrate_current_offset(bms_model_t *model, int32_t raw_offset) {
    if(raw_offset > MIN_CURRENT_OFFSET && raw_offset < MAX_CURRENT_OFFSET) {
        info_printf("Current calibration complete: offset=%ld\n", raw_offset);
        model->current_offset = raw_offset;
        return true;
    } else {
        error_printf("Current calibration out of range: %ld\n", raw_offset);
        return false;
    }
}
