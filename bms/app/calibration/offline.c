#include "offline.h"
#include "common.h"

#include "drivers/chip/nvm.h"
#include "drivers/sensors/ads1115.h"
#include "drivers/sensors/ina228.h"
#include "app/model.h"
#include "sys/logging/logging.h"

// TODO - move these somewhere
extern ads1115_t ads1115_dev;
extern ina228_t ina228_dev;

bool finish_calibration(bms_model_t *model) {
    float sample_count = (float)OFFLINE_VOLTAGE_CALIBRATION_SAMPLES;

    if(!calibrate_battery_voltage(model, ads1115_get_calibration(&ads1115_dev, 0), sample_count)) {
        return false;
    }

    if(!calibrate_output_voltage(model, ads1115_get_calibration(&ads1115_dev, 1), sample_count)) {
        return false;
    }

    if(!calibrate_neg_contactor(model, ads1115_get_calibration(&ads1115_dev, 2), sample_count)) {
        return false;
    }

    if(!calibrate_pos_contactor(model, ads1115_get_calibration(&ads1115_dev, 3), sample_count)) {
        return false;
    }

    int32_t current_offset = (int32_t)div_round_closest(
        ina228_dev.null_accumulator,
        ina228_dev.null_counter
    );
    if(!calibrate_current_offset(model, current_offset)) {
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
                ads1115_start_calibration(&ads1115_dev, OFFLINE_VOLTAGE_CALIBRATION_SAMPLES);
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
