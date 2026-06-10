# EKF Refactor Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Refactor `ekf.c` to pass `bms_model_t *model` explicitly to `ekf_tick`, fix the broken 100%-SoC init heuristic, and clean up `ekf_auto_init` into named phases.

**Architecture:** `ekf_tick` gains a `model` parameter (consistent with all other functions in the file); `ekf_auto_init` is split into a named `estimate_initial_soc` helper and renamed to `ekf_init_from_model`; the `charge_used_Ah != 0.0f` guard is removed and the tolerance tightened to 10%.

**Tech Stack:** C11, CMocka unit tests, Docker test runner (`./tests/run_tests.sh`).

---

## Files

| File | Change |
|------|--------|
| `bms/app/estimators/ekf.h` | Update `ekf_tick` declaration |
| `bms/app/estimators/ekf.c` | `ekf_tick` signature, rename `ekf_auto_init`, add `estimate_initial_soc`, fix init logic, remove dead code, update comment |
| `bms/app/bms.c` | Update `ekf_tick` call site |
| `tests/test_soc.c` | Update 2 `ekf_tick` call sites, add 1 new test |

---

## Task 1: Update `ekf_tick` signature and all callers

This is a mechanical, compile-time-verified change. No logic changes yet — `ekf_auto_init` remains as-is.

**Files:**
- Modify: `bms/app/estimators/ekf.h:49`
- Modify: `bms/app/estimators/ekf.c:487-512`
- Modify: `bms/app/bms.c:172-176`
- Modify: `tests/test_soc.c:79,98`

- [ ] **Step 1: Update the declaration in `ekf.h`**

In `bms/app/estimators/ekf.h`, change line 49:

```c
// Before:
uint32_t ekf_tick(float charge_Ah, int32_t current_mA, int32_t voltage_mV);

// After:
uint32_t ekf_tick(bms_model_t *model, float charge_Ah, int32_t current_mA, int32_t voltage_mV);
```

- [ ] **Step 2: Update the definition in `ekf.c`**

Replace the `ekf_tick` function (lines 487–512 in `bms/app/estimators/ekf.c`):

```c
uint32_t ekf_tick(bms_model_t *model, float charge_Ah, int32_t current_mA, int32_t voltage_mV) {
    float current_amps = (float)current_mA * 0.001f;
    float voltage_volts = (float)voltage_mV * 0.001f;

    ekf_auto_init(model, voltage_volts);

    if (!model->ekf.initialized) {
        return 0xFFFFFFFF;
    }

    ekf_step(&model->ekf, charge_Ah, current_amps, voltage_volts);

    ekf_update_limits(model);

    float soc = ekf_get_soc(&model->ekf);
    // Apply scaling
    soc = (soc - model->ekf.prev_soc_min) * model->ekf.prev_soc_mul;

    if (soc < 0.0f) soc = 0.0f;
    if (soc > 1.0f) soc = 1.0f;

    model->charge_used_Ah = model->ekf.x[0];

    return (uint32_t)(soc * 10000.0f); // Return SOC in 0.01% units
}
```

The old body used `model.` (global access); this uses `model->` throughout. The internal call to `ekf_auto_init` already took a pointer — only pass site changes.

- [ ] **Step 3: Update the call site in `bms/app/bms.c`**

Around line 172, change:

```c
        uint32_t soc = ekf_tick(
            raw_charge_to_Ah(model.charge_raw - last_charge_raw),
            model.current_mA,
            model.cell_voltage_total_mV / NUM_CELLS
        );
```

to:

```c
        uint32_t soc = ekf_tick(
            &model,
            raw_charge_to_Ah(model.charge_raw - last_charge_raw),
            model.current_mA,
            model.cell_voltage_total_mV / NUM_CELLS
        );
```

- [ ] **Step 4: Update call sites in `tests/test_soc.c`**

Line 79 (inside `test_ekf_soc_scaling_midrange`):
```c
// Before:
uint32_t soc_out = ekf_tick(0, 0, 3750);
// After:
uint32_t soc_out = ekf_tick(&model, 0, 0, 3750);
```

Line 98 (inside `test_ekf_soc_scaling_top`):
```c
// Before:
uint32_t soc_out = ekf_tick(0, 0, 3690);
// After:
uint32_t soc_out = ekf_tick(&model, 0, 0, 3690);
```

- [ ] **Step 5: Run tests to verify they still pass**

```bash
./tests/run_tests.sh
```

Expected: all tests pass (same behaviour as before — no logic change yet).

- [ ] **Step 6: Commit**

```bash
git add bms/app/estimators/ekf.h bms/app/estimators/ekf.c bms/app/bms.c tests/test_soc.c
git commit -m "refactor(ekf): pass model explicitly to ekf_tick

Removes the hidden global dependency. ekf_tick now matches the
calling convention of every other function in the file.

Co-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>"
```

---

## Task 2: Add failing test for the 100%-SoC init bug

The current `ekf_auto_init` has `charge_used_Ah != 0.0f` as a guard, which prevents using the stored SoC when the battery is fully charged (0 Ah used = 100% SoC). This test will **fail** against the current code and **pass** after Task 3 fixes it.

**Files:**
- Modify: `tests/test_soc.c`

**Background:** `get_cell_voltage_working_max_mV` clamps at `soft_max - 25`. With a zeroed model, NMC `DEFAULT_CELL_VOLTAGE_SOFT_MAX_mV = 4200` and `CELL_VOLTAGE_HARD_MAX_mV = 4250`, so `soft_max = min(4200, 4225) = 4200` and the ceiling is `4175 mV`. Setting `cell_voltage_working_max_mV = 4175` therefore hits exactly `4175 mV`.

At `4175 mV` → absolute SoC ≈ 98.5% on the NMC curve.  
At `4150 mV` (test voltage) → absolute SoC ≈ 97.4%.  
Stored: `charge_used_Ah = 0` → absolute SoC = 100%.  
Diff = 2.6% < 10% tolerance → new code uses stored; old code skips it (`!= 0.0f`).

- [ ] **Step 1: Add the new test function before `main()` in `tests/test_soc.c`**

```c
static void test_ekf_init_full_charge(void **state) {
    (void)state;

    memset(&model, 0, sizeof(bms_model_t));
    model.nameplate_capacity_mC = 100 * 3600 * 1000; // 100 Ah

    // Working range that reaches near 100% absolute SoC on the NMC curve.
    // working_max 4175 mV is the ceiling after soft-limit clamping (soft_max=4200, -25).
    model.cell_voltage_working_min_mV = 3300;
    model.cell_voltage_working_max_mV = 4175;

    // Stored: 0 Ah used = 100% SoC. The old code skipped this because of
    // the `!= 0.0f` guard; the new code uses it because diff vs voltage is ~2.6%.
    model.charge_used_Ah = 0.0f;

    // Voltage at 4150 mV/cell — 97.4% absolute SoC on NMC curve, inside working range.
    uint32_t soc_out = ekf_tick(&model, 0, 0, 4150);

    // With the fix: initialises from stored (100%), result clamped to 100.00% → 10000.
    // Without the fix: initialises from voltage (~97.4%), result ≈ 98.8% → ~9880.
    assert_true(soc_out > 9950);
}
```

- [ ] **Step 2: Register the test in `main()`**

In `tests/test_soc.c`, add to the `tests[]` array in `main()`:

```c
const struct CMUnitTest tests[] = {
    cmocka_unit_test(test_ekf_soc_scaling_midrange),
    cmocka_unit_test(test_ekf_soc_scaling_top),
    cmocka_unit_test(test_ekf_init_full_charge),   // <-- add this line
    cmocka_unit_test(test_inverter_soc_scaling),
};
```

- [ ] **Step 3: Run tests and confirm the new test fails**

```bash
./tests/run_tests.sh
```

Expected: `test_ekf_init_full_charge` **FAILS** (soc_out ≈ 9880, which is ≤ 9950). All other tests still pass.

---

## Task 3: Refactor `ekf_auto_init` into named phases and fix the init bug

Replace `ekf_auto_init` with two functions: a pure `estimate_initial_soc` helper that clearly explains the voltage-vs-stored choice, and `ekf_init_from_model` that calls it. Fix the `!= 0.0f` bug and tighten the tolerance.

**Files:**
- Modify: `bms/app/estimators/ekf.c:441-462`

- [ ] **Step 1: Replace `ekf_auto_init` with the two new functions**

In `bms/app/estimators/ekf.c`, replace the entire `ekf_auto_init` function (lines 441–462) with:

```c
static float estimate_initial_soc(const bms_model_t *model, float voltage_volts, float capacity_ah) {
    float voltage_soc = ocv_to_soc(voltage_volts);
    if (model->charge_used_Ah >= 0.0f && model->charge_used_Ah <= capacity_ah) {
        float stored_soc = 1.0f - (model->charge_used_Ah / capacity_ah);
        // Accept stored value only if within 10% of voltage estimate.
        // No != 0 guard needed: zero Ah used is valid at 100% SoC, and the tolerance
        // already rejects an uninitialized zero (stored=100%, voltage≈50% → 0.5 diff).
        if (fabsf(stored_soc - voltage_soc) < 0.1f)
            return stored_soc;
    }
    return voltage_soc;
}

static void ekf_init_from_model(bms_model_t *model, float voltage_volts) {
    if (model->ekf.initialized || voltage_volts <= 0.0f) return;
    float capacity_ah = (float)model->nameplate_capacity_mC / 3600000.0f;
    float initial_soc = estimate_initial_soc(model, voltage_volts, capacity_ah);
    info_printf("EKF init: SOC %.2f%% (voltage %.3fV, stored %.4fAh)\n",
                initial_soc * 100.0f, voltage_volts, model->charge_used_Ah);
    ekf_init(&model->ekf, initial_soc, capacity_ah);
}
```

- [ ] **Step 2: Update the call inside `ekf_tick`**

In the `ekf_tick` body (written in Task 1), change the call from `ekf_auto_init` to `ekf_init_from_model`:

```c
// Before:
    ekf_auto_init(model, voltage_volts);

// After:
    ekf_init_from_model(model, voltage_volts);
```

- [ ] **Step 3: Run tests and confirm they all pass**

```bash
./tests/run_tests.sh
```

Expected: all tests pass, including `test_ekf_init_full_charge`.

- [ ] **Step 4: Commit**

```bash
git add bms/app/estimators/ekf.c tests/test_soc.c
git commit -m "fix(ekf): fix 100%-SoC init, refactor auto-init into named phases

- Extract estimate_initial_soc() helper that clearly documents the
  voltage-vs-stored decision
- Rename ekf_auto_init -> ekf_init_from_model
- Remove the broken charge_used_Ah != 0.0f guard (zero Ah used is
  valid at 100% SoC; the 10% tolerance already rejects a zero-
  initialised struct)
- Tighten tolerance from 20% to 10%

Co-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>"
```

---

## Task 4: Clean up `ekf_update_limits` and dead code in `ekf_init`

Two small readability improvements: document *why* `working_capacity_mC` lives in `ekf_update_limits`, and remove a redundant double-assignment in `ekf_init`.

**Files:**
- Modify: `bms/app/estimators/ekf.c`

- [ ] **Step 1: Remove dead parameter assignments in `ekf_init`**

In `ekf_init` (around line 259), there are two consecutive sets of `R0 / R1 / C1`. The first three lines are immediately overwritten and never used:

```c
// Remove these three lines + their comment:
    // Model Parameters (Example Cell)
    ekf->R0 = 0.02f;
    ekf->R1 = 0.03f;
    ekf->C1 = 2000.0f;

// Keep only the fitted params (rename the comment):
    // Fitted RC model parameters
    ekf->R0 = 0.00076f;
    ekf->R1 = 0.00054f;
    ekf->C1 = 150000.0f;
```

- [ ] **Step 2: Replace the TODO in `ekf_update_limits` with an explanation**

In `ekf_update_limits` (around line 484), change:

```c
    // TODO - Move this capacity calculation somewhere more appropriate
    model->working_capacity_mC = (soc_max - model->ekf.prev_soc_min) * (float)model->nameplate_capacity_mC;
```

to:

```c
    // working_capacity_mC is the usable subset of nameplate capacity given the current
    // working voltage range. Computed here because it shares the same min/max limit
    // inputs as the SOC scaling factors above.
    model->working_capacity_mC = (soc_max - model->ekf.prev_soc_min) * (float)model->nameplate_capacity_mC;
```

- [ ] **Step 3: Run tests**

```bash
./tests/run_tests.sh
```

Expected: all tests pass.

- [ ] **Step 4: Commit**

```bash
git add bms/app/estimators/ekf.c
git commit -m "refactor(ekf): remove dead code in ekf_init, document working_capacity intent

Co-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>"
```
