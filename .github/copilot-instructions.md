# GitHub Copilot Instructions for RP2350 BMS Project

## Project Overview
This repository contains C11 firmware for the BMS controller running on RP235x (Pico SDK + CMake). The current codebase is organized under `bms/` with app logic, drivers, protocols, and system utilities split by domain.

## Current Project Layout

- `main.c`: firmware entry point (`main()`) with core1 launch, `bms_init()`, `bms_tick()`, `synchronize_time()` loop.
- `bms/app/`: high-level application logic
  - `bms.c`: main tick pipeline
  - `init.c`: hardware/comms/model initialization
  - `model.h/.c`: global model, persistent structs, derived calculations
  - `battery/`: balancing, current limits, safety checks
  - `state_machines/`: `base`, `system`, `contactors`
  - `calibration/`: calibration state machine + helpers
  - `estimators/`: EKF + alternative SoC estimators
  - `monitoring/`: runtime counters
- `bms/drivers/`: low-level device/peripheral drivers
  - `bmb3y/`, `isospi/`, `contactors/`, `sensors/`, `comms/`, `chip/`
- `bms/protocols/`: external/internal interfaces
  - `cli/`, `hmi_serial/`, `internal_serial/`, `inverter/`
- `bms/sys/`: shared system services (`events/`, `logging/`, `time/`)
- `bms/config/`: limits, pins, settings, allocations, board headers
- `tests/`: host-side CMocka unit tests and coverage tooling

## Architecture & Patterns

### 1) Global model-first design (`bms/app/model.h`)
- System state is centralized in global `bms_model_t model`.
- Most modules read/write `model` fields directly.
- `bms_model_t` includes:
  - live measurements + timestamps
  - derived limits/estimates
  - state-machine contexts (`system_sm`, `contactor_sm`, `balancing_sm`, `calibration_sm`)
  - persistent settings split into:
    - `persistent_fast` (frequent NVM writes)
    - `persistent_slow` (infrequent calibration/config writes)

### 2) Main loop pipeline (`bms/app/bms.c`)
Each tick follows a phased flow:
1. watchdog + preamble
2. read inputs/sensors
3. update model + estimators
4. run safety/hardware checks + events processing
5. tick state machines
6. run protocol/comms outputs and NVM maintenance
7. optional debug output/timing metrics

### 3) State machine framework (`bms/app/state_machines/base.h`)
- Use `sm_t` (anonymous-struct embedding is common in this repo).
- Initialize with `sm_init()`.
- Transition with `state_transition()`.
- Use `state_timeout()` / `state_time()` for time-based logic.
- Implement per-module `*_sm_tick(bms_model_t *model)` functions.

### 4) Active state machines
- `system_sm_tick()`: high-level mode lifecycle (`INITIALIZING`, `INACTIVE`, `OPERATING`, `CALIBRATING`, `FAULT`) and request handling.
- `contactor_sm_tick()`: sequencing/tests/precharge/opening behavior.
- `balancing_sm_tick()`: automatic cell balancing logic (called from BMB cycle).
- `calibration_sm_tick()`: offline/online calibration flows.

### 5) Timing model (`bms/sys/time/time.h`)
- Tick period target is `TIMESTEP_PERIOD_MS` (20 ms).
- Use `millis()`, `millis64()`, `timestep()` helpers.
- Validate freshness with `millis_recent_enough(...)` before acting on sensor data.
- `synchronize_time()` enforces loop cadence and reports overruns.

### 6) Safety/event model (`bms/sys/events`)
- Event levels: `NONE`, `INFO`, `WARNING`, `CRITICAL`, `FATAL`.
- `confirm(...)` / `check_or_confirm(...)` are the standard pattern for condition + event management.
- FATAL events drive system fault behavior (contactor opening path).

## Drivers & Protocol Notes

- BMB communications are in `bms/drivers/bmb3y/` over ISOSPI (`bms/drivers/isospi/`).
- Sensor drivers include INA228, ADS1115, and internal ADC under `bms/drivers/sensors/`.
- Serial packet links use DUART in `bms/drivers/comms/duart.c`.
- Inverter integration lives in `bms/protocols/inverter/` (CAN-based).
- USB CLI command parsing is in `bms/protocols/cli/cli.c`.

## Persistence / NVM

- NVM support is in `bms/drivers/chip/nvm.c`.
- LittleFS is used for persisted blobs and boot count.
- Respect versioned persistent structs in `model.h`.
- Do not reorder existing persisted fields without version migration.

## Build & Test Workflow

### Firmware build
```bash
mkdir -p build
cd build
cmake ..
make -j4
```
Primary output: `build/bms.uf2`.

### Unit tests (host)
```bash
cd tests
mkdir -p build
cd build
cmake ..
ctest --output-on-failure
```
Tests are CMocka-based and defined in `tests/CMakeLists.txt`.

## Coding conventions for this repo

- Language: C11 (`-fms-extensions` enabled; anonymous struct embedding is used intentionally).
- Naming: `snake_case` for functions/variables; enums/macros in project style.
- Prefer minimal, targeted changes; preserve existing style.
- Safety-first defaults:
  - check data staleness before control decisions
  - keep event semantics consistent (`confirm`/`clear` patterns)
  - keep contactor and state-machine transitions explicit and conservative
- When changing control logic, update/add unit tests in `tests/` where practical.

## Key files to consult first

- `main.c`
- `bms/app/bms.c`
- `bms/app/init.c`
- `bms/app/model.h`
- `bms/app/state_machines/base.h`
- `bms/app/state_machines/system.c`
- `bms/app/state_machines/contactors.c`
- `bms/sys/events/events.h`
- `bms/sys/time/time.h`
- `tests/CMakeLists.txt`
