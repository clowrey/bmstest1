#pragma once

#include "../state_machines/base.h"

typedef struct bms_model bms_model_t;

typedef struct {
    // Anonymous base struct
    sm_t; 
    uint16_t initial_contactor_state;
} online_calibration_sm_t;

enum online_calibration_states {
    ONLINE_CALIBRATION_STATE_IDLE = 0,
    ONLINE_CALIBRATION_STATE_STABILIZING = 1,
    ONLINE_CALIBRATION_STATE_MEASURING = 2,
};

typedef enum online_calibration_requests {
    ONLINE_CALIBRATION_REQUEST_NULL = 0,
    ONLINE_CALIBRATION_REQUEST_START = 1,
} online_calibration_requests_t;

void online_calibration_sm_tick(bms_model_t *model);
