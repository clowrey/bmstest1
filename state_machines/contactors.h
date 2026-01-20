#pragma once

#include "base.h"

typedef struct bms_model bms_model_t;

typedef struct {
    // Anonymous base struct
    sm_t; 
    // Current flow is allowed (contactors are closed and not opening)
    bool enable_current;
} contactors_sm_t;

#define CONTACTORS_STATES(X)                      \
    X(CONTACTORS_STATE_OPEN,                   0) \
    X(CONTACTORS_STATE_TESTING_NEG_OPEN,       1) \
    X(CONTACTORS_STATE_TESTING_NEG_CLOSED,     2) \
    X(CONTACTORS_STATE_TESTING_POS_OPEN,       3) \
    X(CONTACTORS_STATE_TESTING_POS_CLOSED,     4) \
    X(CONTACTORS_STATE_PRECHARGING_NEG,        5) \
    X(CONTACTORS_STATE_PRECHARGING,            6) \
    X(CONTACTORS_STATE_CLOSED,                 7) \
    X(CONTACTORS_STATE_CALIBRATING,            8) \
    X(CONTACTORS_STATE_CALIBRATING_CLOSE_NEG,  9) \
    X(CONTACTORS_STATE_CALIBRATING_PRECHARGE, 10) \
    X(CONTACTORS_STATE_CALIBRATING_CLOSED,    11) \
    X(CONTACTORS_STATE_PRECHARGE_FAILED,      12) \
    X(CONTACTORS_STATE_TESTING_FAILED,        13)

enum contactors_states {
#define X(name, value) name = value,
    CONTACTORS_STATES(X)
#undef X
};

typedef enum contactors_requests {
    CONTACTORS_REQUEST_NULL = 0,
    CONTACTORS_REQUEST_CLOSE = 1,
    CONTACTORS_REQUEST_OPEN = 2,
    CONTACTORS_REQUEST_FORCE_OPEN = 3,
    CONTACTORS_REQUEST_CALIBRATE = 4,
} contactors_requests_t;

void contactor_sm_tick(bms_model_t *model);
