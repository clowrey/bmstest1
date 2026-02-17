#include "offline.h"

#include "drivers/chip/nvm.h"
#include "drivers/sensors/ads1115.h"
#include "drivers/sensors/ina228.h"
#include "app/model.h"
#include "sys/logging/logging.h"

// TODO - move these somewhere
extern ads1115_t ads1115_dev;
extern ina228_t ina228_dev;

#define VOLTAGE_CALIBRATION_SAMPLES 1024

// cal mul value should be around 0.013 Volts/count
#define MIN_CALIBRATION_MUL 0.008f
#define MAX_CALIBRATION_MUL 0.018f

// offset should be within a few volts
#define MIN_NEG_CONTACTOR_OFFSET_MV -5000
#define MAX_NEG_CONTACTOR_OFFSET_MV 5000

// current offset should be within +/- 200
#define MIN_CURRENT_OFFSET -200
#define MAX_CURRENT_OFFSET 200

bool finish_calibration(bms_model_t *model) {
    int32_t batcal = ads1115_get_calibration(&ads1115_dev, 0);
    float mul = (float)model->cell_voltage_total_mV * 0.001f * (float)VOLTAGE_CALIBRATION_SAMPLES / (float)batcal;

    if(mul > MIN_CALIBRATION_MUL && mul < MAX_CALIBRATION_MUL) {
        info_printf("Calibration complete: cal0=%ld mul0=%f cvt=%ld\n", batcal, mul, model->cell_voltage_total_mV);
        model->battery_voltage_mul = mul;
    } else {
        error_printf("Battery voltage calibration out of range: %f\n", mul);
        return false;
    }

    int32_t outcal = ads1115_get_calibration(&ads1115_dev, 1);
    mul = (float)model->cell_voltage_total_mV * 0.001f * (float)VOLTAGE_CALIBRATION_SAMPLES / (float)outcal;

    if(mul > MIN_CALIBRATION_MUL && mul < MAX_CALIBRATION_MUL) {
        info_printf("Calibration complete: cal1=%ld mul1=%f\n", outcal, mul);
        model->output_voltage_mul = mul;
    } else {
        error_printf("Output voltage calibration out of range: %f\n", mul);
        return false;
    }

    // lazily split the difference for now, not quite right but it'll do
    model->neg_contactor_mul = (model->battery_voltage_mul + model->output_voltage_mul) / 2.0f;

    // calculate the offset
    int32_t negcal = ads1115_get_calibration(&ads1115_dev, 2);
    float neg_mV = (float)negcal * model->neg_contactor_mul * 1000.0f / (float)VOLTAGE_CALIBRATION_SAMPLES;

    debug_printf("Raw negcal=%ld neg_mV=%f, neg_contactor_mul=%f\n", negcal, neg_mV, model->neg_contactor_mul);

    if(neg_mV > MIN_NEG_CONTACTOR_OFFSET_MV && neg_mV < MAX_NEG_CONTACTOR_OFFSET_MV) {
        info_printf("Neg contactor calibration complete: cal2=%ld neg_mV=%f\n", negcal, neg_mV);
        model->neg_contactor_offset_mV = -neg_mV;
    } else {
        error_printf("Neg contactor calibration out of range: %f mV\n", neg_mV);
        return false;
    }

    int32_t bat_pos_to_out_neg_cal = ads1115_get_calibration(&ads1115_dev, 3);
    mul = (float)model->cell_voltage_total_mV * 0.001f * (float)VOLTAGE_CALIBRATION_SAMPLES / (float)bat_pos_to_out_neg_cal;

    if(mul > MIN_CALIBRATION_MUL && mul < MAX_CALIBRATION_MUL) {
        info_printf("Pos contactor calibration complete: cal3=%ld mul3=%f\n", bat_pos_to_out_neg_cal, mul);
        model->pos_contactor_mul = mul;
    } else {
        error_printf("Pos contactor calibration out of range: %f\n", mul);
        return false;
    }

    int32_t current_offset = div_round_closest(
        ina228_dev.null_accumulator,
        ina228_dev.null_counter
    );
    if(current_offset > MIN_CURRENT_OFFSET && current_offset < MAX_CURRENT_OFFSET) {
        info_printf("Current calibration complete: offset=%ld\n", current_offset);
        model->current_offset = current_offset;
    } else {
        error_printf("Current calibration out of range: %ld\n", current_offset);
        return false;
    }

    return true;
}

void offline_calibration_sm_tick(bms_model_t *model) {
    offline_calibration_sm_t *sm = &(model->offline_calibration_sm);
    switch(sm->state) {
        case OFFLINE_CALIBRATION_STATE_IDLE:
            // Wait for command to start calibration
            if(model->offline_calibration_req == OFFLINE_CALIBRATION_REQUEST_START) {
                model->offline_calibration_req = OFFLINE_CALIBRATION_REQUEST_NULL;
                state_transition((sm_t*)sm, OFFLINE_CALIBRATION_STATE_WAITING_FOR_CONTACTORS);
            }
            break;
        case OFFLINE_CALIBRATION_STATE_WAITING_FOR_CONTACTORS:
            // Wait until contactors are in the right state
            if(model->contactor_sm.state == CONTACTORS_STATE_CALIBRATING_CLOSED) {
                state_transition((sm_t*)sm, OFFLINE_CALIBRATION_STATE_STABILIZING);
            } else if(state_timeout((sm_t*)sm, 10000)) {
                // Timeout waiting for contactors to close
                // Should we error? give up
                state_transition((sm_t*)sm, OFFLINE_CALIBRATION_STATE_IDLE);
            }
            break;
        case OFFLINE_CALIBRATION_STATE_STABILIZING:
            // Wait a bit for voltages to stabilize
            if(state_timeout((sm_t*)sm, 2000)) {
                ads1115_start_calibration(&ads1115_dev, VOLTAGE_CALIBRATION_SAMPLES);
                ina228_dev.null_accumulator = 0;
                ina228_dev.null_counter = 0;
                state_transition((sm_t*)sm, OFFLINE_CALIBRATION_STATE_MEASURING);
            }
            break;
        case OFFLINE_CALIBRATION_STATE_MEASURING:
            if(model->contactor_sm.state != CONTACTORS_STATE_CALIBRATING_CLOSED) {
                // Contactors opened unexpectedly, give up
                state_transition((sm_t*)sm, OFFLINE_CALIBRATION_STATE_IDLE);
            } else if(ads1115_calibration_finished(&ads1115_dev)) {
                if(finish_calibration(model)) {
                    nvm_save_persistent_slow(model);
                }

                state_transition((sm_t*)sm, OFFLINE_CALIBRATION_STATE_IDLE);
            }
            break;
        default:
            // unknown state, go to idle
            state_transition((sm_t*)sm, OFFLINE_CALIBRATION_STATE_IDLE);
            break;
    }
}
