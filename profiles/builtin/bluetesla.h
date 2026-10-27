#pragma once

// 96-cell NMC pack

#include "chemistry/nmc.h"

// Cell presence bitmask (1 = present, 0 = not present, 32-bit groups, LSB = cell 0)
#define CELL_PRESENCE_WORDS 0xc7ff8f7f, 0xf3ffe3ff, 0xfcfff8ff, 0x1ffe3d
#define NUM_CELLS 96
#define NUM_MODULE_VOLTAGES 8
#define NUM_MODULE_TEMPS 8
#define NAMEPLATE_CAPACITY_AH 147
#define INA228_SHUNT_CAL 332
#define INVERTER_MODEL_STRING "CellKeeper Tesla NMC 1\x00\x00\x00\x00\x00"

// Absolute maximum current limits
#define CHARGE_MAX_CURRENT_dA 300 // 30A
#define DISCHARGE_MAX_CURRENT_dA 300 // 30A
