#pragma once

#include "app/state_machines/base.h"
#include <stdint.h>
#include <stdbool.h>

#define VOLTAGE_CALIBRATION_SAMPLES 1024
#define SHORT_VOLTAGE_CALIBRATION_SAMPLES 64


typedef struct bms_model bms_model_t;

typedef enum {
    CALIBRATION_TYPE_OFFLINE_LONG,
    CALIBRATION_TYPE_OFFLINE_SHORT,
    CALIBRATION_TYPE_ONLINE,
} calibration_type_t;

typedef enum {
    CALIBRATION_STATE_IDLE = 0,
    
    // OFFLINE SHORT sequence
    CALIBRATION_STATE_SHORT_WAIT_OPEN,
    CALIBRATION_STATE_SHORT_STABILIZING_OPEN,
    CALIBRATION_STATE_SHORT_MEASURING_OPEN,
    
    CALIBRATION_STATE_SHORT_WAIT_NEG,
    CALIBRATION_STATE_SHORT_STABILIZING_NEG,
    CALIBRATION_STATE_SHORT_MEASURING_NEG,

    // OFFLINE LONG sequence
    CALIBRATION_STATE_LONG_WAIT_BOTH,
    CALIBRATION_STATE_LONG_STABILIZING,
    CALIBRATION_STATE_LONG_MEASURING,

    // ONLINE sequence
    CALIBRATION_STATE_ONLINE_STABILIZING,
    CALIBRATION_STATE_ONLINE_MEASURING,
} calibration_state_t;

typedef enum {
    CALIBRATION_REQUEST_NULL = 0,
    CALIBRATION_REQUEST_START_OFFLINE_LONG = 1,
    CALIBRATION_REQUEST_START_OFFLINE_SHORT = 2,
    CALIBRATION_REQUEST_START_ONLINE = 3,
} calibration_requests_t;

typedef struct {
    // Anonymous base struct
    sm_t; 
    uint16_t initial_contactor_state;
} calibration_sm_t;

void calibration_sm_tick(bms_model_t *model);
