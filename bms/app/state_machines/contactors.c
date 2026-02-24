#include "contactors.h"
#include "config/limits.h"
#include "app/model.h"
#include "sys/events/events.h"
#include "sys/logging/logging.h"
#include "drivers/contactors/contactors.h"
#include "sys/time/time.h"

#include <stdio.h>

// TODO - as well as staleness, check for noisy readings (or use max value sometimes?)
// TODO - give up after so many failed precharges

// Contactor testing constants

// How long to wait for voltage to settle during contactor tests
#define CONTACTORS_TEST_WAIT_MS 1000
// How long to wait after a failed precharge (for the PTCs to cool down)
#define CONTACTORS_FAILED_PRECHARGE_TIMEOUT_MS 20000
// How long to wait after a failed contactor test (to avoid rapid cycling)
#define CONTACTORS_FAILED_TEST_TIMEOUT_MS 20000
// Max voltage across a contactor to consider it closed
#define CONTACTORS_CLOSED_VOLTAGE_THRESHOLD_MV 2000
// Min voltage across a contactor to consider it open
#define CONTACTORS_OPEN_VOLTAGE_THRESHOLD_MV 3000 // was 5000
// Pos contactor has wider tolerances due to the way it is measured
#define CONTACTORS_POS_CLOSED_VOLTAGE_THRESHOLD_MV 7000
#define CONTACTORS_POS_OPEN_VOLTAGE_THRESHOLD_MV 10000


// TODO : more compensation for ADC resistor discrepancies, to avoid lopsided readings?
// could just calibrate it? seems to specifically be a problem for pos due to it being a derived reading

// Precharge constants

// Largest allowable pack current to consider precharge successful
#define PRECHARGE_SUCCESS_MAX_MA 1000
// Maximum voltage difference to consider precharge successful
#define PRECHARGE_SUCCESS_MAX_MV 3000

// Contactor opening constants

// Current below which we can instantly open contactors
#define CONTACTORS_INSTANT_OPEN_MA 1000
// Current below which we can open contactors (after a delay)
#define CONTACTORS_DELAYED_OPEN_MA 5000
// How long we wait for the current to fall before potentially failing to open contactors
#define CONTACTORS_OPEN_TIMEOUT_MS 2000
// How long we wait when force-opening contactors
#define CONTACTORS_FORCE_OPEN_TIMEOUT_MS 2000

static inline int32_t abs_int32(int32_t v) {
    return (v < 0) ? -v : v;
}

static inline int32_t clamp(int32_t v, int32_t min, int32_t max) {
    if(v < min) {
        return min;
    } else if(v > max) {
        return max;
    }
    return v;
}


bool check_current_is_below(bms_model_t *model, int32_t threshold_ma) {
    if(!millis_recent_enough(model->current_millis, CURRENT_STALE_THRESHOLD_MS)) {
        // stale reading
        //ret.event_type = ERR_CURRENT_STALE;
        //ret.data64 = model->current_millis;
        return false;
    }
    
    if(abs_int32(model->current_mA) > threshold_ma) {
        debug_printf("Erk, current %d mA is above threshold %d mA\n", abs_int32(model->current_mA), threshold_ma);
    }

    return abs_int32(model->current_mA) <= threshold_ma;

    // if(abs_int32(model->current_mA)<=threshold_ma) {
    //     ret.success = true;
    //     return ret;
    // }

    // ret.event_type = ERR_CONTACTOR_PRECHARGE_CURRENT_TOO_HIGH;
    // ret.data64 = model->current_mA;
    // return ret;
}

bool check_precharge_successful(bms_model_t *model, bool log_errors) {
    if(!check_or_confirm(
        millis_recent_enough(model->battery_voltage_millis, BATTERY_VOLTAGE_STALE_THRESHOLD_MS),
        log_errors,
        ERR_BATTERY_VOLTAGE_STALE,
        0x1000000000000000
    )) {
        return false;
    }

    if(!check_or_confirm(
        millis_recent_enough(model->output_voltage_millis, OUTPUT_VOLTAGE_STALE_THRESHOLD_MS),
        log_errors,
        ERR_CONTACTOR_PRECHARGE_VOLTAGE_TOO_HIGH,
        0x2000000000000000
    )) {
        return false;
    }

    if(!check_or_confirm(
        millis_recent_enough(model->neg_contactor_voltage_millis, CONTACTOR_VOLTAGE_STALE_THRESHOLD_MS),
        log_errors,
        ERR_CONTACTOR_NEG_UNEXPECTED_OPEN,
        0x3000000000000000
    )) {
        return false;
    }

    if(!check_or_confirm(
        millis_recent_enough(model->current_millis, CURRENT_STALE_THRESHOLD_MS),
        log_errors,
        ERR_CURRENT_STALE,
        0x4000000000000000
    )) {
        return false;
    }

    if(!check_or_confirm(
        fabsf(model->neg_contactor_voltage) <= (CONTACTORS_CLOSED_VOLTAGE_THRESHOLD_MV * 0.001f),
        log_errors,
        ERR_CONTACTOR_NEG_UNEXPECTED_OPEN,
        (int32_t)(model->neg_contactor_voltage * 1000)
    )) {
        return false;
    }

    // Is current still too high?
    if(!check_or_confirm(
        abs_int32(model->current_mA) <= PRECHARGE_SUCCESS_MAX_MA,
        log_errors,
        ERR_CONTACTOR_PRECHARGE_CURRENT_TOO_HIGH,
        model->current_mA
    )) {
        return false;
    }

    // Is voltage difference low enough?
    return check_or_confirm(
        fabsf(model->battery_voltage - model->output_voltage) <= (PRECHARGE_SUCCESS_MAX_MV * 0.001f),
        log_errors,
        ERR_CONTACTOR_PRECHARGE_VOLTAGE_TOO_HIGH,
        ((uint64_t)(model->battery_voltage * 1000) << 32) | (uint32_t)(model->output_voltage * 1000)
    );
}

bool confirm_battery_is_healthy(bms_model_t *model) {
    // Do we need this as well as the events system?

    return true;
}

bool confirm_contactor_neg_seems_closed(bms_model_t *model) {
    if(!confirm(
        millis_recent_enough(model->neg_contactor_voltage_millis, CONTACTOR_VOLTAGE_STALE_THRESHOLD_MS),
        ERR_CONTACTOR_NEG_STUCK_OPEN,
        0x1000000000000000
    )) {
        return false;
    }

    return confirm(
        fabsf(model->neg_contactor_voltage) <= (CONTACTORS_CLOSED_VOLTAGE_THRESHOLD_MV * 0.001f),
        ERR_CONTACTOR_NEG_STUCK_OPEN,
        (int32_t)(model->neg_contactor_voltage * 1000)
    );
}

bool confirm_contactor_neg_seems_open(bms_model_t *model) {
    if(!confirm(
        millis_recent_enough(model->neg_contactor_voltage_millis, CONTACTOR_VOLTAGE_STALE_THRESHOLD_MS),
        ERR_CONTACTOR_NEG_STUCK_CLOSED,
        0x1000000000000000
    )) {
        return false;
    }

    float voltage = fabsf(model->neg_contactor_voltage);

    if(voltage < (CONTACTORS_OPEN_VOLTAGE_THRESHOLD_MV * 0.001f)) {
        debug_printf("Erk, neg contactor voltage %1.3f V is below open threshold %d mV\n", model->neg_contactor_voltage, CONTACTORS_OPEN_VOLTAGE_THRESHOLD_MV);
    }

    return confirm(
        voltage >= (CONTACTORS_OPEN_VOLTAGE_THRESHOLD_MV * 0.001f),
        ERR_CONTACTOR_NEG_STUCK_CLOSED,
        (int32_t)(voltage * 1000)
    );

    // TODO - also check for voltage difference between battery and output?
}

bool confirm_contactor_pos_seems_closed(bms_model_t *model) {
    if(!confirm(
        millis_recent_enough(model->pos_contactor_voltage_millis, CONTACTOR_VOLTAGE_STALE_THRESHOLD_MS),
        ERR_CONTACTOR_POS_STUCK_OPEN,
        0x1000000000000000
    )) {
        return false;
    }

    float voltage = fabsf(model->pos_contactor_voltage);
    return confirm(
        voltage <= (CONTACTORS_POS_CLOSED_VOLTAGE_THRESHOLD_MV * 0.001f),
        ERR_CONTACTOR_POS_STUCK_OPEN,
        (int32_t)(voltage * 1000)
    );
}

bool confirm_contactor_pos_seems_open(bms_model_t *model) {
    if(!confirm(
        millis_recent_enough(model->pos_contactor_voltage_millis, CONTACTOR_VOLTAGE_STALE_THRESHOLD_MS),
        ERR_CONTACTOR_POS_STUCK_CLOSED,
        0x1000000000000000
    )) {
        return false;
    }

    float voltage = fabsf(model->pos_contactor_voltage);

    if(voltage < (CONTACTORS_POS_OPEN_VOLTAGE_THRESHOLD_MV * 0.001f)) {
        debug_printf("Erk, pos contactor voltage %1.3f V is below open threshold %d mV\n", model->pos_contactor_voltage, CONTACTORS_POS_OPEN_VOLTAGE_THRESHOLD_MV);
    }

    return confirm(
        voltage >= (CONTACTORS_POS_OPEN_VOLTAGE_THRESHOLD_MV * 0.001f),
        ERR_CONTACTOR_POS_STUCK_CLOSED,
        (int32_t)(voltage * 1000)
    );

    // TODO - also check for voltage difference between battery and output?
}

bool confirm_contactor_pos_and_neg_seems_open(bms_model_t *model) {
    bool ret = true;
    if(!confirm_contactor_pos_seems_open(model)) ret = false;
    if(!confirm_contactor_neg_seems_open(model)) ret = false;
    return ret;
}

bool confirm_contactor_pre_seems_closed(bms_model_t *model) {
    // Use the aux contact reading
    return confirm(
        model->precharge_closed,
        ERR_CONTACTOR_PRE_STUCK_OPEN,
        0
    );
}

bool confirm_contactor_pre_seems_open(bms_model_t *model) {
    // Use the aux contact reading
    return confirm(
        !model->precharge_closed,
        ERR_CONTACTOR_PRE_STUCK_CLOSED,
        0
    );
}

bool confirm_contactors_staying_closed(bms_model_t *model) {
    bool ret = true;

    // Check that the contactors still seem to be closed, and haven't opened due
    // to a contactor supply glitch. If they have opened we will need to go
    // through the checking/precharge sequence again.

    // Note, we will return true if the readings are stale, as we don't want to
    // immediately trigger contactor opening if there is a loop overrun. If it
    // remains stale for too long then the events system will trigger a FATAL
    // and open contactors anyway.

    // (an alternative would be to have another state such that we only consider
    // them open if another timeout passes)

    if(confirm(
        millis_recent_enough(model->pos_contactor_voltage_millis, CONTACTOR_VOLTAGE_STALE_THRESHOLD_MS),
        ERR_CONTACTOR_POS_UNEXPECTED_OPEN,
        0x1000000000000000
    )) {
        float pos_voltage = fabsf(model->pos_contactor_voltage);
        ret = confirm(
            pos_voltage <= (CONTACTORS_CLOSED_VOLTAGE_THRESHOLD_MV * 0.001f),
            ERR_CONTACTOR_POS_UNEXPECTED_OPEN,
            (int32_t)(pos_voltage * 1000)
        ) && ret;
    }

    if(confirm(
        millis_recent_enough(model->neg_contactor_voltage_millis, CONTACTOR_VOLTAGE_STALE_THRESHOLD_MS),
        ERR_CONTACTOR_NEG_UNEXPECTED_OPEN,
        0x2000000000000000
    )) {
        float neg_voltage = fabsf(model->neg_contactor_voltage);
        ret = confirm(
            neg_voltage <= (CONTACTORS_CLOSED_VOLTAGE_THRESHOLD_MV * 0.001f),
            ERR_CONTACTOR_NEG_UNEXPECTED_OPEN,
            (int32_t)(neg_voltage * 1000)
        ) && ret;
    }

    // TODO - check for voltage diference between battery and output?
    // or check for voltage across precharge?

    return ret;
}

void contactor_sm_tick(bms_model_t *model) {
    contactors_sm_t *contactor_sm = &(model->contactor_sm);

    // By default, disallow current flow
    contactor_sm->enable_current = false;

    // React to open requests immediately
    if(model->contactor_req == CONTACTORS_REQUEST_OPEN || model->contactor_req == CONTACTORS_REQUEST_FORCE_OPEN) {
        switch(contactor_sm->state) {
            case CONTACTORS_STATE_PRECHARGING_NEG:
            case CONTACTORS_STATE_PRECHARGING:
            case CONTACTORS_STATE_TESTING_NEG_OPEN:
            case CONTACTORS_STATE_TESTING_NEG_CLOSED:
            case CONTACTORS_STATE_TESTING_POS_OPEN:
            case CONTACTORS_STATE_TESTING_POS_CLOSED:
            case CONTACTORS_STATE_CALIBRATING:
            case CONTACTORS_STATE_CALIBRATING_CLOSE_NEG:
            case CONTACTORS_STATE_CALIBRATING_PRECHARGE:
            case CONTACTORS_STATE_CALIBRATING_CLOSED:
                // Open contactors immediately
                model->contactor_req = CONTACTORS_REQUEST_NULL;
                state_transition((sm_t*)contactor_sm, CONTACTORS_STATE_OPEN);
                break;
            case CONTACTORS_STATE_OPEN:
            case CONTACTORS_STATE_CLOSED:
            case CONTACTORS_STATE_AWAITING_OPEN:
            case CONTACTORS_STATE_PRECHARGE_FAILED:
            case CONTACTORS_STATE_TESTING_FAILED:
                // let normal processing handle it
                break;
        }
    }

    switch(contactor_sm->state) {
        case CONTACTORS_STATE_OPEN:
            contactors_set_pos_pre_neg(false, false, false);
            if(model->contactor_req == CONTACTORS_REQUEST_CLOSE) {
                model->contactor_req = CONTACTORS_REQUEST_NULL;

                if(confirm_battery_is_healthy(model)) {
                    // Start a self-test of the contactors before precharging
                    state_transition((sm_t*)contactor_sm, CONTACTORS_STATE_TESTING_PRE_CLOSED);
                }
            } else if(model->contactor_req == CONTACTORS_REQUEST_OPEN || model->contactor_req == CONTACTORS_REQUEST_FORCE_OPEN) {
                // already open, just clear the request
                model->contactor_req = CONTACTORS_REQUEST_NULL;
            } else if(model->contactor_req == CONTACTORS_REQUEST_CALIBRATE) {
                model->contactor_req = CONTACTORS_REQUEST_NULL;
                state_transition((sm_t*)contactor_sm, CONTACTORS_STATE_CALIBRATING);
            }
            break;
        case CONTACTORS_STATE_CLOSED:
            contactors_set_pos_pre_neg(true, false, true);

            bool try_to_open = (
                // Requesting to open
                model->contactor_req == CONTACTORS_REQUEST_OPEN || 
                model->contactor_req == CONTACTORS_REQUEST_FORCE_OPEN ||
                // or settled but contactors seem to have opened unexpectedly
                (state_timeout((sm_t*)contactor_sm, 2000) && !confirm_contactors_staying_closed(model))
            );

            if(state_timeout((sm_t*)contactor_sm, 500) && !try_to_open) {
                // Enable current flow after a short delay
                contactor_sm->enable_current = true;
            }

            if(try_to_open) {
                state_transition((sm_t*)contactor_sm, CONTACTORS_STATE_AWAITING_OPEN);
            } else if(model->contactor_req == CONTACTORS_REQUEST_CLOSE) {
                // cancel any close requests (we're already closed)
                model->contactor_req = CONTACTORS_REQUEST_NULL;
            }
             
            break;
        case CONTACTORS_STATE_AWAITING_OPEN:
            contactors_set_pos_pre_neg(true, false, true);

            // Wait for current to fall before actually opening contactors.
            if((
                    check_current_is_below(model, CONTACTORS_INSTANT_OPEN_MA)
                    || (check_current_is_below(model, CONTACTORS_DELAYED_OPEN_MA) && state_timeout((sm_t*)contactor_sm, CONTACTORS_OPEN_TIMEOUT_MS))
            ) || (
                model->contactor_req == CONTACTORS_REQUEST_FORCE_OPEN && (
                    check_current_is_below(model, CONTACTORS_INSTANT_OPEN_MA)
                    || state_timeout((sm_t*)contactor_sm, CONTACTORS_FORCE_OPEN_TIMEOUT_MS)
                )
            )) {
                 model->contactor_req = CONTACTORS_REQUEST_NULL;
                 state_transition((sm_t*)contactor_sm, CONTACTORS_STATE_OPEN);
            }
            break;
        case CONTACTORS_STATE_TESTING_PRE_CLOSED:
            contactors_test_pre(true);
            if(state_timeout((sm_t*)contactor_sm, CONTACTORS_TEST_WAIT_MS)) {
                if(confirm_contactor_pre_seems_closed(model)) {
                    // passed
                    state_transition((sm_t*)contactor_sm, CONTACTORS_STATE_TESTING_NEG_OPEN);
                } else {
                    // fault detected
                    state_transition((sm_t*)contactor_sm, CONTACTORS_STATE_TESTING_FAILED);
                }
            }
            break;
        case CONTACTORS_STATE_TESTING_NEG_OPEN:
            contactors_set_pos_pre_neg(false, false, false);

            if(state_timeout((sm_t*)contactor_sm, CONTACTORS_TEST_WAIT_MS)) {
                if(confirm_contactor_pos_and_neg_seems_open(model)) {
                    // passed
                    state_transition((sm_t*)contactor_sm, CONTACTORS_STATE_TESTING_NEG_CLOSED);
                } else {
                    // fault detected
                    count_bms_event(
                        ERR_CONTACTOR_CLOSING_FAILED,
                        0x2000000000000000
                    );
                    state_transition((sm_t*)contactor_sm, CONTACTORS_STATE_TESTING_FAILED);
                }
            }
            break;
        case CONTACTORS_STATE_TESTING_NEG_CLOSED:
            contactors_set_pos_pre_neg(false, false, true);

            if(state_timeout((sm_t*)contactor_sm, CONTACTORS_TEST_WAIT_MS)) {
                if(confirm_contactor_neg_seems_closed(model)) {
                    // passed
                    state_transition((sm_t*)contactor_sm, CONTACTORS_STATE_TESTING_POS_OPEN);
                } else {
                    // fault detected
                    count_bms_event(
                        ERR_CONTACTOR_CLOSING_FAILED,
                        0x3000000000000000
                    );
                    state_transition((sm_t*)contactor_sm, CONTACTORS_STATE_TESTING_FAILED);
                }
            }
            break;
        case CONTACTORS_STATE_TESTING_POS_OPEN:
            contactors_set_pos_pre_neg(false, false, false);

            if(state_timeout((sm_t*)contactor_sm, CONTACTORS_TEST_WAIT_MS)) {
                if(confirm_contactor_pos_and_neg_seems_open(model)) {
                    // all tests passed
                    state_transition((sm_t*)contactor_sm, CONTACTORS_STATE_TESTING_POS_CLOSED);
                } else {
                    // fault detected
                    count_bms_event(
                        ERR_CONTACTOR_CLOSING_FAILED,
                        0x4000000000000000
                    );
                    state_transition((sm_t*)contactor_sm, CONTACTORS_STATE_TESTING_FAILED);
                }
            }
            break;
        case CONTACTORS_STATE_TESTING_POS_CLOSED:
            // Both positive and precharge (since this actually leaves the
            // precharge open due to the inverted logic - we don't want to close
            // more than one contactor per state due to current draw)
            contactors_set_pos_pre_neg(true, true, false);

            float voltage = fabsf(model->pos_contactor_voltage);
            debug_printf("Pos contactor voltage: %1.3f V\n", voltage);

            if(state_timeout((sm_t*)contactor_sm, CONTACTORS_TEST_WAIT_MS)) {
                if(confirm_contactor_pos_seems_closed(model)) {
                    // all tests passed, go to precharging
                    state_transition((sm_t*)contactor_sm, CONTACTORS_STATE_PRECHARGING_NEG);
                } else {
                    // fault detected
                    count_bms_event(
                        ERR_CONTACTOR_CLOSING_FAILED,
                        0x5000000000000000
                    );
                    state_transition((sm_t*)contactor_sm, CONTACTORS_STATE_TESTING_FAILED);
                }
            }
            break;
        case CONTACTORS_STATE_TESTING_FAILED:
            contactors_set_pos_pre_neg(false, false, false);
            // Wait for some arbitrary time to avoid cycling too fast
            if(state_timeout((sm_t*)contactor_sm, CONTACTORS_FAILED_TEST_TIMEOUT_MS)) {
                state_transition((sm_t*)contactor_sm, CONTACTORS_STATE_OPEN);
            }
            break;

        case CONTACTORS_STATE_PRECHARGING_NEG:
            // Close negative contactor first
            contactors_set_pos_pre_neg(false, false, true);
            if(!confirm_contactor_pre_seems_open(model)) {
                // precharge contactor is stuck closed
                state_transition((sm_t*)contactor_sm, CONTACTORS_STATE_PRECHARGE_FAILED);
            } else if(state_timeout((sm_t*)contactor_sm, 500)) {
                state_transition((sm_t*)contactor_sm, CONTACTORS_STATE_PRECHARGING);
            }
            break;
        case CONTACTORS_STATE_PRECHARGING:
            // Now close precharge contactor (actually just the Bat+ one)
            contactors_set_pos_pre_neg(false, true, true);

            debug_printf("PRECHARGING: %1.3f V, %d mA\n", 
                model->battery_voltage - model->output_voltage,
                model->current_mA
            );


            if(model->contactor_req == CONTACTORS_REQUEST_OPEN || model->contactor_req == CONTACTORS_REQUEST_FORCE_OPEN) {
                // Abort precharge
                model->contactor_req = CONTACTORS_REQUEST_NULL;
                state_transition((sm_t*)contactor_sm, CONTACTORS_STATE_OPEN);
            } else if(state_timeout((sm_t*)contactor_sm, 1000) && check_precharge_successful(model, false)) {
                // Successful precharge
                info_printf("Precharge successful after %u ms\n", state_time((sm_t*)contactor_sm));
                state_transition((sm_t*)contactor_sm, CONTACTORS_STATE_CLOSED);
            } else if(state_timeout((sm_t*)contactor_sm, 10000)) {
                // Failed to precharge!
                // Log the reason
                check_precharge_successful(model, true);
                count_bms_event(
                    ERR_CONTACTOR_CLOSING_FAILED,
                    0x1000000000000000
                );
                state_transition((sm_t*)contactor_sm, CONTACTORS_STATE_PRECHARGE_FAILED);
            }
            break;

        case CONTACTORS_STATE_PRECHARGE_FAILED:
            contactors_set_pos_pre_neg(false, false, false);
            // wait for the precharge to cool down
            if(state_timeout((sm_t*)contactor_sm, CONTACTORS_FAILED_PRECHARGE_TIMEOUT_MS)) {
                state_transition((sm_t*)contactor_sm, CONTACTORS_STATE_OPEN);
            }            
            break;            

        /* Calibration (requires contactors to be closed) */
        
        case CONTACTORS_STATE_CALIBRATING:
            contactors_set_pos_pre_neg(false, false, false);

            if(state_timeout((sm_t*)contactor_sm, CONTACTORS_TEST_WAIT_MS)) {
                state_transition((sm_t*)contactor_sm, CONTACTORS_STATE_CALIBRATING_CLOSE_NEG);
            }
            break;
        case CONTACTORS_STATE_CALIBRATING_CLOSE_NEG:
            // Close negative contactor first
            contactors_set_pos_pre_neg(false, false, true);
            if(state_timeout((sm_t*)contactor_sm, CONTACTORS_TEST_WAIT_MS)) {
                state_transition((sm_t*)contactor_sm, CONTACTORS_STATE_CALIBRATING_PRECHARGE);
            }
            break;
        case CONTACTORS_STATE_CALIBRATING_PRECHARGE:
            // Now precharge
            contactors_set_pos_pre_neg(false, true, true);
            if(state_timeout((sm_t*)contactor_sm, CONTACTORS_TEST_WAIT_MS)) {
                // There should be no real precharging as nothing should be
                // attached. We need a margin to accommodate for uncalibrated
                // values however.
                debug_printf("[11] checking current\n");
                if(check_current_is_below(model, 200)) {
                    debug_printf("[11] pass!\n");
                    state_transition((sm_t*)contactor_sm, CONTACTORS_STATE_CALIBRATING_CLOSED);
                } else {
                    debug_printf("[11] fail!\n");
                    // Error properly?
                    state_transition((sm_t*)contactor_sm, CONTACTORS_STATE_OPEN);
                }
            }
            break;
        case CONTACTORS_STATE_CALIBRATING_CLOSED:
            contactors_set_pos_pre_neg(true, false, true);

            if(!check_current_is_below(model, 200)) {
                // Current too high (output is meant to be open circuit!)
                // TODO - Error properly?
                state_transition((sm_t*)contactor_sm, CONTACTORS_STATE_OPEN);
            }
            break;
            
        default:
            // panic instead?
            error_printf("Invalid contactor state!");
            state_reset((sm_t*)contactor_sm);
            break;
    }
}
