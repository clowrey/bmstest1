#include "online.h"
#include "common.h"

#include "drivers/chip/nvm.h"
#include "drivers/sensors/ads1115.h"
#include "drivers/sensors/ina228.h"
#include "app/model.h"
#include "sys/logging/logging.h"

// TODO - move these somewhere
extern ads1115_t ads1115_dev;
extern ina228_t ina228_dev;

static bool finish_online_calibration(bms_model_t *model) {
    bool ok = false;

    float sample_count = (float)ONLINE_VOLTAGE_CALIBRATION_SAMPLES;

    if (model->contactor_sm.state == CONTACTORS_STATE_OPEN) {
        // Calibrate battery voltage and current sensors
        if (calibrate_battery_voltage(model, ads1115_get_calibration(&ads1115_dev, 0), sample_count)) {
            int32_t current_offset = (int32_t)div_round_closest(
                ina228_dev.null_accumulator,
                ina228_dev.null_counter
            );
            if (calibrate_current_offset(model, current_offset)) {
                ok = true;
            }
        }
    } else if (model->contactor_sm.state == CONTACTORS_STATE_CLOSED) {
        // Calibrate contactor voltages and output voltage
        if (calibrate_output_voltage(model, ads1115_get_calibration(&ads1115_dev, 1), sample_count)) {
            if (calibrate_neg_contactor(model, ads1115_get_calibration(&ads1115_dev, 2), sample_count)) {
                if (calibrate_pos_contactor(model, ads1115_get_calibration(&ads1115_dev, 3), sample_count)) {
                    ok = true;
                }
            }
        }
    } else {
        error_printf("Online calibration failed: contactors in invalid state %d\n", model->contactor_sm.state);
        return false;
    }
    
    return ok;
}

void online_calibration_sm_tick(bms_model_t *model) {
    online_calibration_sm_t *sm = &(model->online_calibration_sm);
    
    switch(sm->state) {
        case ONLINE_CALIBRATION_STATE_IDLE:
            if (model->online_calibration_req == ONLINE_CALIBRATION_REQUEST_START) {
                if (model->contactor_sm.state != CONTACTORS_STATE_OPEN && model->contactor_sm.state != CONTACTORS_STATE_CLOSED) {
                    error_printf("Online calibration aborted: contactors in state %d\n", model->contactor_sm.state);
                    model->online_calibration_req = ONLINE_CALIBRATION_REQUEST_NULL;
                } else {
                    sm->initial_contactor_state = model->contactor_sm.state;
                    model->online_calibration_req = ONLINE_CALIBRATION_REQUEST_NULL;
                    state_transition((sm_t*)sm, ONLINE_CALIBRATION_STATE_STABILIZING);
                }
            }
            break;
            
        case ONLINE_CALIBRATION_STATE_STABILIZING:
            // Wait a bit for voltages to stabilize
            if (state_timeout((sm_t*)sm, 2000)) {
                ads1115_start_calibration(&ads1115_dev, ONLINE_VOLTAGE_CALIBRATION_SAMPLES);
                ina228_dev.null_accumulator = 0;
                ina228_dev.null_counter = 0;
                state_transition((sm_t*)sm, ONLINE_CALIBRATION_STATE_MEASURING);
            }
            break;
            
        case ONLINE_CALIBRATION_STATE_MEASURING:
            if (model->contactor_sm.state != sm->initial_contactor_state) {
                // Contactors changed state unexpectedly, give up
                error_printf("Online calibration aborted: contactor state changed from %d to %d\n", sm->initial_contactor_state, model->contactor_sm.state);
                state_transition((sm_t*)sm, ONLINE_CALIBRATION_STATE_IDLE);
            } else if (ads1115_calibration_finished(&ads1115_dev)) {
                if(finish_online_calibration(model)) {
                    nvm_save_persistent_slow(model);
                }
                state_transition((sm_t*)sm, ONLINE_CALIBRATION_STATE_IDLE);
            }
            break;
            
        default:
            state_transition((sm_t*)sm, ONLINE_CALIBRATION_STATE_IDLE);
            break;
    }
}
