#include "system.h"

#include "../hw/sensors/ina228.h"
#include "../limits.h"
#include "../model.h"

bool successfully_initialized(bms_model_t *model) {
    // check we're getting readings from everything we need

    if(!millis_recent_enough(model->battery_voltage_millis, BATTERY_VOLTAGE_STALE_THRESHOLD_MS)) {
        return false;
    }

    if(!millis_recent_enough(model->output_voltage_millis, OUTPUT_VOLTAGE_STALE_THRESHOLD_MS)) {
        return false;
    }

    if(!millis_recent_enough(model->neg_contactor_voltage_millis, CONTACTOR_VOLTAGE_STALE_THRESHOLD_MS)) {
        return false;
    }

    if(!millis_recent_enough(model->pos_contactor_voltage_millis, CONTACTOR_VOLTAGE_STALE_THRESHOLD_MS)) {
        return false;
    }

    if(!millis_recent_enough(model->current_millis, 1000)) {
        return false;
    }

    if(!millis_recent_enough(model->cell_voltage_millis, CELL_VOLTAGE_STALE_THRESHOLD_MS)) {
        return false;
    }

    return true;
}


void system_sm_tick(bms_model_t *model) {
    system_sm_t *system_sm = &(model->system_sm);
    switch(system_sm->state) {
        case SYSTEM_STATE_UNINITIALIZED:
            state_transition((sm_t*)system_sm, SYSTEM_STATE_INITIALIZING);
            break;
        case SYSTEM_STATE_INITIALIZING:
            //extern ina228_t ina228_dev;
            bool calibrated = true;//ina228_dev.null_counter == 64;

            if(calibrated && successfully_initialized(model)) {
                state_transition((sm_t*)system_sm, SYSTEM_STATE_INACTIVE);
            }
            // assert some events after a timeout?
            break;
        case SYSTEM_STATE_INACTIVE:
            if(model->system_req == SYSTEM_REQUEST_RUN) {
                // leave request asserted?
                //model->system_req = SYSTEM_REQUEST_NULL;
                state_transition((sm_t*)system_sm, SYSTEM_STATE_OPERATING);
            }
            break;
        case SYSTEM_STATE_OPERATING:
            // Keep trying to close? (TODO: check failure count?)
            model->contactor_req = CONTACTORS_REQUEST_CLOSE;

            break;
        case SYSTEM_STATE_FAULT:
            break;
    }
}
