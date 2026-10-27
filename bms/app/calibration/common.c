#include "common.h"
#include "sys/logging/logging.h"
#include <math.h>

bool calibrate_battery_voltage(bms_model_t *model, int32_t raw_val, float sample_count) {
    float mul = (float)model->cell_voltage_total_mV * 0.001f * sample_count / (float)raw_val;

    if(mul > MIN_CALIBRATION_MUL && mul < MAX_CALIBRATION_MUL) {
        info_printf("Battery voltage calibration complete: raw=%" PRId32 " mul=%f cvt=%" PRId32 "\n", raw_val, mul, model->cell_voltage_total_mV);
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
        info_printf("Output voltage calibration complete: raw=%" PRId32 " mul=%f\n", raw_val, mul);
        model->output_voltage_mul = mul;
        return true;
    } else {
        error_printf("Output voltage calibration out of range: %f\n", mul);
        return false;
    }
}

// Calibrate a contactor that is measured directly and reads ~0V while closed,
// by recording its offset. There's no reference voltage across it to calibrate
// a multiplier against, so the multiplier is derived from the two rails it
// sits between. The model is only updated if everything is in range.
static bool calibrate_contactor_offset(
    const char *name,
    int32_t raw_val,
    float sample_count,
    float rail_a_mul,
    float rail_b_mul,
    float min_offset_mV,
    float max_offset_mV,
    float *mul_out,
    float *offset_mV_out
) {
    // lazily split the difference for now, not quite right but it'll do.
    // Use the multipliers actually in effect, since uncalibrated ones are zero.
    float mul = (effective_voltage_mul(rail_a_mul) + effective_voltage_mul(rail_b_mul)) / 2.0f;

    if(!(mul > MIN_CALIBRATION_MUL && mul < MAX_CALIBRATION_MUL)) {
        error_printf("%s contactor calibration multiplier out of range: %f\n", name, mul);
        return false;
    }

    // calculate the offset
    float contactor_mV = (float)raw_val * mul * 1000.0f / sample_count;

    debug_printf("Raw %s cal=%" PRId32 " mV=%f, mul=%f\n", name, raw_val, contactor_mV, mul);

    if(contactor_mV > min_offset_mV && contactor_mV < max_offset_mV) {
        info_printf("%s contactor calibration complete: raw=%" PRId32 " mV=%f\n", name, raw_val, contactor_mV);
        *mul_out = mul;
        *offset_mV_out = -contactor_mV;
        return true;
    } else {
        error_printf("%s contactor calibration out of range: %f mV\n", name, contactor_mV);
        return false;
    }
}

bool calibrate_neg_contactor(bms_model_t *model, int32_t raw_val, float sample_count) {
    return calibrate_contactor_offset(
        "Neg", raw_val, sample_count,
        model->battery_voltage_mul, model->output_voltage_mul,
        MIN_NEG_CONTACTOR_OFFSET_MV, MAX_NEG_CONTACTOR_OFFSET_MV,
        &model->neg_contactor_mul, &model->neg_contactor_offset_mV
    );
}

#if IS_BMS_BOARD(REV1)

bool calibrate_link_voltage(bms_model_t *model, int32_t raw_val, float sample_count) {
    // Channel 7 measures LinkNeg - LinkPos, so negate to get the link voltage.
    // With the contactors closed the link rail sits at the battery voltage.
    float mul = (float)model->cell_voltage_total_mV * 0.001f * sample_count / (float)(-raw_val);

    if(mul > MIN_CALIBRATION_MUL && mul < MAX_CALIBRATION_MUL) {
        info_printf("Link voltage calibration complete: raw=%" PRId32 " mul=%f\n", raw_val, mul);
        model->link_voltage_mul = mul;
        return true;
    } else {
        error_printf("Link voltage calibration out of range: %f\n", mul);
        return false;
    }
}

bool calibrate_pos_contactor(bms_model_t *model, int32_t raw_val, float sample_count) {
    // Rev1 measures directly across the pos contactor, which reads ~0V while
    // the contactors are closed, so calibrate the offset (like the neg
    // contactor). Should run after battery and link voltage calibration.
    return calibrate_contactor_offset(
        "Pos", raw_val, sample_count,
        model->battery_voltage_mul, model->link_voltage_mul,
        MIN_POS_CONTACTOR_OFFSET_MV, MAX_POS_CONTACTOR_OFFSET_MV,
        &model->pos_contactor_mul, &model->pos_contactor_offset_mV
    );
}

#else

bool calibrate_pos_contactor(bms_model_t *model, int32_t raw_val, float sample_count) {
    float mul = (float)model->cell_voltage_total_mV * 0.001f * sample_count / (float)raw_val;

    if(mul > MIN_CALIBRATION_MUL && mul < MAX_CALIBRATION_MUL) {
        info_printf("Pos contactor calibration complete: raw=%" PRId32 " mul=%f\n", raw_val, mul);
        model->pos_contactor_mul = mul;
        return true;
    } else {
        error_printf("Pos contactor calibration out of range: %f\n", mul);
        return false;
    }
}

#endif

bool calibrate_current_offset(bms_model_t *model, int32_t raw_offset) {
    if(raw_offset > MIN_CURRENT_OFFSET && raw_offset < MAX_CURRENT_OFFSET) {
        info_printf("Current calibration complete: offset=%" PRId32 "\n", raw_offset);
        model->current_offset = raw_offset;
        return true;
    } else {
        error_printf("Current calibration out of range: %" PRId32 "\n", raw_offset);
        return false;
    }
}
