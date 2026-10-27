#pragma once

// 15-cell NMC desk test rig

#include "chemistry/nmc.h"

// Cell presence bitmask (1 = present, 0 = not present, 32-bit groups, LSB = cell 0)
#define CELL_PRESENCE_WORDS 0x7FFF, 0, 0, 0
#define NUM_CELLS 15
#define NUM_MODULE_VOLTAGES 1
#define NUM_MODULE_TEMPS 1
#define NAMEPLATE_CAPACITY_AH 1
#define INA228_SHUNT_CAL 3088

// Absolute maximum current limits
#define CHARGE_MAX_CURRENT_dA 300 // 30A
#define DISCHARGE_MAX_CURRENT_dA 300 // 30A
